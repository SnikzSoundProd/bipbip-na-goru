// bipbip_editor — Unreal-style level editor for «БИП БИП НА ГОРУ».
// Docks: Toolbar / Outliner / Details / Viewport.
// Edit mode  = author the scene (boxes, holds, seed) and save .bipscene
// Play mode  = PIE world built from a Play-time snapshot; Stop restores Edit.
// Live edits while playing are hot-applied (Unreal requires stopping PIE).
#include "platform/window_win32.h"
#include "render/dx11_device.h"
#include "render/shader_manager.h"
#include "render/mesh.h"
#include "render/camera.h"
#include "render/frustum.h"
#include "render/text_renderer.h"
#include "world/heightfield.h"
#include "physics/verlet.h"
#include "core/scene.h"
#include "pie_world.h"
#include "camera_fly.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <windows.h>
#include <io.h>

using namespace bip;

// ---- Win32 ImGui message hook (installed into the shared window proc) ----
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

std::string projectRoot() {
    wchar_t buf[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::string cur = "./";
    if (n) {
        int wlen = WideCharToMultiByte(CP_UTF8, 0, buf, (int)n, nullptr, 0, nullptr, nullptr);
        std::string tmp(wlen, '\0');
        WideCharToMultiByte(CP_UTF8, 0, buf, (int)n, tmp.data(), wlen, nullptr, nullptr);
        size_t pos = tmp.find_last_of("/\\");
        cur = (pos != std::string::npos) ? tmp.substr(0, pos + 1) : "./";
    }
    for (int guard = 0; guard < 8; ++guard) {
        if (_access((cur + "assets").c_str(), 0) == 0) return cur;
        size_t sl = cur.find_last_of("/\\", cur.size() - 2);
        if (sl == std::string::npos) break;
        cur = cur.substr(0, sl + 1);
    }
    return cur;
}

// Game-matching palette (same neon-on-dark feel as the in-game HUD)
void styleEditor() {
    ImGuiStyle& st = ImGui::GetStyle();
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg]       = ImVec4(0.06f, 0.07f, 0.10f, 0.95f);
    c[ImGuiCol_TitleBg]        = ImVec4(0.10f, 0.12f, 0.18f, 1.f);
    c[ImGuiCol_TitleBgActive]  = ImVec4(0.16f, 0.75f, 0.62f, 1.f);
    c[ImGuiCol_Header]         = ImVec4(0.16f, 0.75f, 0.62f, 0.35f);
    c[ImGuiCol_HeaderHovered]  = ImVec4(0.16f, 0.75f, 0.62f, 0.55f);
    c[ImGuiCol_HeaderActive]   = ImVec4(0.16f, 0.75f, 0.62f, 0.75f);
    c[ImGuiCol_Button]         = ImVec4(0.14f, 0.16f, 0.22f, 1.f);
    c[ImGuiCol_ButtonHovered]  = ImVec4(0.16f, 0.75f, 0.62f, 0.7f);
    c[ImGuiCol_ButtonActive]   = ImVec4(0.16f, 0.75f, 0.62f, 1.f);
    c[ImGuiCol_FrameBg]        = ImVec4(0.10f, 0.11f, 0.15f, 1.f);
    c[ImGuiCol_Border]         = ImVec4(0.16f, 0.75f, 0.62f, 0.35f);
    c[ImGuiCol_Text]           = ImVec4(0.88f, 0.92f, 0.95f, 1.f);
    c[ImGuiCol_CheckMark]      = ImVec4(0.16f, 0.75f, 0.62f, 1.f);
    c[ImGuiCol_SliderGrab]     = ImVec4(0.16f, 0.75f, 0.62f, 1.f);
    st.WindowRounding = 4.f;
    st.FrameRounding  = 3.f;
    st.GrabRounding   = 3.f;
}

struct Editor {
    Scene      scene;          // authored (Edit-mode source of truth)
    PIEWorld   pie;            // Play-In-Editor world
    EditorMode mode = EditorMode::Edit;
    int        selected = -1;  // index into scene.boxes (or holds if selIsHold)
    bool       selIsHold = false;
    std::string path;
    char statusMsg[256] = "";

    // viewport camera
    Camera cam;
    bool   camOrbit = false;
    HeightField editHf;        // Edit-mode terrain (for picking/drop-to-ground)

    // ---- smooth camera transitions (Unreal-style "fly to") ----
    bool   camFollowPlayer = false; // Play mode: camera trails the player

    // smooth camera transitions (shared, unit-tested implementation)
    CameraFly camFly;

    // begin a smooth flight to a world position
    void flyTo(const Vec3& target, float yaw, float pitch) {
        camFly.start(cam.pos, target, cam.yaw, yaw, cam.pitch, pitch);
    }

    // Frame an object: pull the camera to a comfortable distance from `pos`,
    // keeping the current viewing direction (Unreal's "F" focus behaviour).
    void focusOn(const Vec3& pos, float radius) {
        Vec3 dir = cam.pos - pos;
        float len = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
        if (len < 0.001f) dir = Vec3{0.f, 0.35f, 1.f};
        else              dir = dir * (1.f / len);
        float dist = std::max(7.f, radius * 4.5f);
        Vec3 dest = pos + dir * dist;
        float yaw   = atan2f(-dir.x, -dir.z);
        float pitch = asinf(std::max(-1.f, std::min(1.f, -dir.y)));
        camFollowPlayer = false;
        flyTo(dest, yaw, pitch);
    }

    void updateCamera(float dt) {
        if (camFly.flying()) {
            Vec3 p; float y, pt;
            camFly.update(dt, &p, &y, &pt);
            cam.pos = p; cam.yaw = y; cam.pitch = pt;
        }
    }
};

// Holds as authored in the scene (Edit mode has no PIE route yet).
const std::vector<Hold>& authoredHolds(Editor& ed) {
    static std::vector<Hold> cache;
    // rebuild only when the authored list changes size (cheap, avoids per-frame alloc)
    if (cache.size() != ed.scene.holds.size()) {
        cache.clear();
        for (const auto& sh : ed.scene.holds)
            cache.push_back(Hold{ Vec3{sh.pos[0], sh.pos[1], sh.pos[2]}, sh.checkpoint });
    } else {
        for (size_t i = 0; i < cache.size(); ++i) {
            cache[i].pos = Vec3{ ed.scene.holds[i].pos[0], ed.scene.holds[i].pos[1], ed.scene.holds[i].pos[2] };
            cache[i].checkpoint = ed.scene.holds[i].checkpoint;
        }
    }
    return cache;
}

// Draw a climber ragdoll (same part boxes the game renders).
void drawClimber(ID3D11DeviceContext* ctx, ID3D11Buffer* cbObj, ID3D11Buffer* cbTint,
                 const Mesh& unitMesh, Climber& c, bool isBuddy) {
    Climber::PartBox parts[64];
    int n = c.collectParts(parts);
    for (int i = 0; i < n; ++i) {
        const auto& pb = parts[i];
        Vec3 y = pb.yAxis;
        Vec3 x = normalize(cross(y, pb.zHint));
        Vec3 z = cross(x, y);
        float sx = pb.half.x * 2.f, sy = pb.half.y * 2.f, sz = pb.half.z * 2.f;
        float w[16] = {
            x.x*sx, x.y*sx, x.z*sx, 0,
            y.x*sy, y.y*sy, y.z*sy, 0,
            z.x*sz, z.y*sz, z.z*sz, 0,
            pb.center.x, pb.center.y, pb.center.z, 1
        };
        ctx->UpdateSubresource(cbObj, 0, nullptr, w, 0, 0);
        // buddy wears teal so you can tell the two apart (as in the game)
        Vec3 col = pb.color;
        bool isShirt = fabsf(col.x - 0.85f) < 0.02f && fabsf(col.y - 0.25f) < 0.03f;
        if (isBuddy && isShirt) col = Vec3{0.2f, 0.75f, 0.7f};
        float colr[4] = { col.x, col.y, col.z, 1 };
        ctx->UpdateSubresource(cbTint, 0, nullptr, colr, 0, 0);
        unitMesh.draw(ctx);
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::string assetRoot = projectRoot();
    std::string scenePath = (argc > 1) ? argv[1] : (assetRoot + "assets/scenes/default.bipscene");

    Window window;
    if (!window.create("BIP BIP NA GORU — EDITOR", 1600, 900)) return 1;
    window.setUiHook([](HWND h, UINT m, WPARAM w, LPARAM l) -> bool {
        return ImGui_ImplWin32_WndProcHandler(h, m, w, l) != 0;
    });

    Dx11Device gfx;
    if (!gfx.init(window.handle(), 1600, 900)) return 2;

    Editor ed;
    ed.path = scenePath;
    if (!loadScene(scenePath, ed.scene)) {
        // no saved scene: author a sensible default so the editor is usable
        ed.scene = Scene{};
        ed.scene.boxes.push_back(SceneBox{});
        ed.scene.boxes.back().pos[1] = 5.f;
    }

    // --- ImGui -----------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;    // full docking
    styleEditor();
    ImGui_ImplWin32_Init(window.handle());
    ImGui_ImplDX11_Init(gfx.device(), gfx.ctx());

    // --- render resources ------------------------------------------------
    ShaderManager shaders;
    if (!shaders.init(gfx.device())) return 3;
    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    ID3D11VertexShader* vs = nullptr; ID3D11InputLayout* il = nullptr;
    ID3D11PixelShader* ps = nullptr; std::string errs;
    if (!shaders.loadVertexShader(assetRoot + "assets/shaders/basic_vs.hlsl", "main", layout, 4, &vs, &il, &errs)) {
        fprintf(stderr, "VS error:\n%s\n", errs.c_str()); return 4;
    }
    if (!shaders.loadPixelShader(assetRoot + "assets/shaders/basic_ps.hlsl", "main", &ps, &errs)) {
        fprintf(stderr, "PS error:\n%s\n", errs.c_str()); return 5;
    }

    struct CBPerFrame { float viewProj[16]; };
    struct CBPerObject { float world[16]; };
    struct CBPerTint  { float tint[4]; };
    ID3D11Buffer* cbFrame = nullptr; ID3D11Buffer* cbObj = nullptr; ID3D11Buffer* cbTint = nullptr;
    auto mkCB = [&](UINT sz, ID3D11Buffer** out) {
        D3D11_BUFFER_DESC d{}; d.Usage = D3D11_USAGE_DEFAULT; d.ByteWidth = sz;
        d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        gfx.device()->CreateBuffer(&d, nullptr, out);
    };
    mkCB(sizeof(CBPerFrame), &cbFrame); mkCB(sizeof(CBPerObject), &cbObj); mkCB(sizeof(CBPerTint), &cbTint);

    Mesh mountain, unitMesh;
    {
        ed.editHf.generate(ed.scene.seed, ed.scene.worldSize, ed.scene.heightN);
        if (!mountain.init(gfx.device(), ed.editHf.vertices().data(), (uint32_t)ed.editHf.vertices().size(),
                           ed.editHf.indices().data(), (uint32_t)ed.editHf.indices().size())) {
            fprintf(stderr, "[editor] mountain mesh init FAILED (vtx=%zu idx=%zu)\n",
                    ed.editHf.vertices().size(), ed.editHf.indices().size());
            return 6;
        }
        fprintf(stderr, "[editor] mountain mesh ok (vtx=%zu idx=%zu)\n",
                ed.editHf.vertices().size(), ed.editHf.indices().size());
    }
    {
        auto vv = geom::box(1.f, 1.f, 1.f);
        auto ii = geom::boxIndices();
        if (!unitMesh.init(gfx.device(), vv.data(), (uint32_t)vv.size(), ii.data(), (uint32_t)ii.size())) {
            fprintf(stderr, "[editor] unit mesh init FAILED\n");
            return 7;
        }
    }

    TextRenderer hud;
    hud.init(gfx.device(), 1600, 900);

    ed.cam.pos = Vec3{0, 95, -190};   // outside the mountain, looking down at it
    ed.cam.pitch = 0.42f;

    InputState input;
    const double kFixedDt = 1.0 / 60.0;
    double acc = 0;

    while (!window.shouldClose()) {
        window.pumpMessages(input);

        // Keep the backbuffer matched to the real window size (the game does
        // this on WM_SIZE). Without it the RT and the UI coordinate space
        // disagree and draws land outside the visible frame.
        {
            RECT r; GetClientRect(window.handle(), &r);
            int w = (int)(r.right - r.left), h = (int)(r.bottom - r.top);
            if (w > 0 && h > 0) gfx.resize(w, h);
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Docking is enabled, but we do NOT force a full-screen dockspace:
        // on a first run with no imgui.ini a dockspace can leave every panel
        // hidden. Free-floating windows are always visible; the user can drag
        // them into a dock layout which then persists in imgui.ini.

        // View matrix + box list are needed by viewport picking, so compute
        // them BEFORE the ImGui panels are built.
        ed.updateCamera((float)kFixedDt);
        if (ed.camFollowPlayer && ed.pie.active()) {
            Vec3 target = ed.pie.player.pelvisPos();
            Vec3 back{ -sinf(ed.cam.yaw)*cosf(ed.cam.pitch), sinf(ed.cam.pitch),
                        -cosf(ed.cam.yaw)*cosf(ed.cam.pitch) };
            Vec3 want = target + back * 6.5f + Vec3{0, 1.6f, 0};
            float groundClear = ed.pie.hf.heightAt(want.x, want.z) + 0.8f;
            if (want.y < groundClear) want.y = groundClear;
            if (ed.camFly.flying()) ed.camFly.retarget(want);
            else                    ed.cam.pos = want;
        }

        float aspect = (float)window.width() / (float)std::max(1, window.height());
        float vp[16];
        ed.cam.viewProj(vp, aspect);

        // Boxes currently visible (PIE live boxes, or the authored scene boxes)
        static std::vector<BoxProp> editBoxes;
        editBoxes.clear();
        const std::vector<BoxProp>* boxesForPick = nullptr;
        if (ed.mode == EditorMode::Play) {
            boxesForPick = &ed.pie.phys.boxes_;
        } else {
            for (const auto& sb : ed.scene.boxes) {
                BoxProp b{};
                b.pos = Vec3{sb.pos[0], sb.pos[1], sb.pos[2]};
                b.hx = sb.half[0]; b.hy = sb.half[1]; b.hz = sb.half[2];
                editBoxes.push_back(b);
            }
            boxesForPick = &editBoxes;
        }

        // ---- Toolbar ----
        ImGui::SetNextWindowPos(ImVec2(324, 32), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(1020, 44), ImGuiCond_FirstUseEver);
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Save", "Ctrl+S")) {
                    if (saveScene(ed.scene, ed.path)) snprintf(ed.statusMsg, sizeof(ed.statusMsg), "Saved %s", ed.path.c_str());
                    else snprintf(ed.statusMsg, sizeof(ed.statusMsg), "SAVE FAILED");
                }
                if (ImGui::MenuItem("Reload")) {
                    if (loadScene(ed.path, ed.scene)) snprintf(ed.statusMsg, sizeof(ed.statusMsg), "Reloaded");
                    else snprintf(ed.statusMsg, sizeof(ed.statusMsg), "LOAD FAILED");
                }
                ImGui::EndMenu();
            }
            bool playing = (ed.mode == EditorMode::Play);
            if (playing) {
                if (ImGui::Button("| Stop")) {
                    ed.pie.stop();
                    ed.mode = EditorMode::Edit;
                    ed.camFollowPlayer = false;
                }
            } else {
                if (ImGui::Button("> Play")) {
                    ed.pie.start(ed.scene, gfx.device());
                    ed.mode = EditorMode::Play;
                    // smoothly fly from the editor camera to the player, then
                    // keep the camera trailing him (Unreal PIE behaviour)
                    Vec3 p = ed.pie.player.pelvisPos();
                    ed.flyTo(p, ed.cam.yaw, 0.32f);
                    ed.camFollowPlayer = true;
                }
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.16f,0.75f,0.62f,1.f), playing ? "PLAYING (PIE)" : "EDIT");
            ImGui::SameLine();
            ImGui::Text("| %s", ed.statusMsg);
            ImGui::SameLine();
            ImGui::TextDisabled(playing ? "WASD move | SPACE jump | LMB/RMB grab" : "");
            ImGui::EndMainMenuBar();
        }

        // ---- Outliner (left) ----
        ImGui::SetNextWindowPos(ImVec2(12, 32), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 420), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Outliner")) {
            if (ImGui::Button("+ Box")) {
                SceneBox b; b.pos[1] = 5.f;
                ed.scene.boxes.push_back(b);
                ed.selected = (int)ed.scene.boxes.size() - 1; ed.selIsHold = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("+ Hold")) {
                SceneHold h; h.pos[1] = 2.f;
                ed.scene.holds.push_back(h);
                ed.selected = (int)ed.scene.holds.size() - 1; ed.selIsHold = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete") && ed.selected >= 0) {
                if (ed.selIsHold) { if (ed.selected < (int)ed.scene.holds.size()) ed.scene.holds.erase(ed.scene.holds.begin() + ed.selected); }
                else              { if (ed.selected < (int)ed.scene.boxes.size()) ed.scene.boxes.erase(ed.scene.boxes.begin() + ed.selected); }
                ed.selected = -1;
            }
            ImGui::Separator();
            ImGui::Text("Boxes (%zu)", ed.scene.boxes.size());
            for (size_t i = 0; i < ed.scene.boxes.size(); ++i) {
                char lbl[64]; snprintf(lbl, sizeof(lbl), "Box %zu", i);
                bool isSel = (!ed.selIsHold && ed.selected == (int)i);
                if (ImGui::Selectable(lbl, isSel)) {
                    ed.selected = (int)i; ed.selIsHold = false;
                    // clicking the name in the outliner flies the camera to it
                    const auto& sb = ed.scene.boxes[i];
                    Vec3 p{sb.pos[0], sb.pos[1], sb.pos[2]};
                    float r = sb.half[0] + sb.half[1] + sb.half[2];
                    ed.focusOn(p, r);
                    snprintf(ed.statusMsg, sizeof(ed.statusMsg), "Box %zu", i);
                }
            }
            ImGui::Text("Holds (%zu)", ed.scene.holds.size());
            for (size_t i = 0; i < ed.scene.holds.size(); ++i) {
                char lbl[64]; snprintf(lbl, sizeof(lbl), "Hold %zu%s", i, ed.scene.holds[i].checkpoint ? " [CP]" : "");
                bool isSel = (ed.selIsHold && ed.selected == (int)i);
                if (ImGui::Selectable(lbl, isSel)) {
                    ed.selected = (int)i; ed.selIsHold = true;
                    const auto& sh = ed.scene.holds[i];
                    Vec3 p{sh.pos[0], sh.pos[1], sh.pos[2]};
                    ed.focusOn(p, 1.0f);
                    snprintf(ed.statusMsg, sizeof(ed.statusMsg), "Hold %zu", i);
                }
            }
            ImGui::End();
        }

        // ---- Details (left, below outliner) ----
        ImGui::SetNextWindowPos(ImVec2(12, 464), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 424), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Details")) {
            if (!ed.selIsHold && ed.selected >= 0 && ed.selected < (int)ed.scene.boxes.size()) {
                auto& b = ed.scene.boxes[(size_t)ed.selected];
                ImGui::Text("Box %d", ed.selected);
                ImGui::DragFloat3("Position", b.pos, 0.1f);
                ImGui::DragFloat3("Half Extents", b.half, 0.05f);
                ImGui::DragFloat3("Rotation (deg)", b.rotDeg, 1.f);
                ImGui::SliderInt("Owner", &b.owner, 0, 1);
            } else if (ed.selIsHold && ed.selected >= 0 && ed.selected < (int)ed.scene.holds.size()) {
                auto& h = ed.scene.holds[(size_t)ed.selected];
                ImGui::Text("Hold %d", ed.selected);
                ImGui::DragFloat3("Position", h.pos, 0.1f);
                ImGui::Checkbox("Checkpoint", &h.checkpoint);
            } else if (!ed.selIsHold && ed.selected < 0) {
                ImGui::Text("World");
                ImGui::InputScalar("Seed", ImGuiDataType_U64, &ed.scene.seed);
                ImGui::DragFloat("World Size", &ed.scene.worldSize, 1.f, 64.f, 1024.f);
                ImGui::DragFloat3("Spawn", ed.scene.spawn, 0.5f);
            } else {
                ImGui::Text("Nothing selected");
            }
            ImGui::Separator();
            // Snap the selected object onto the terrain surface. Without this
            // authored Y values drift underground as the mountain changes.
            bool canDrop = (ed.selected >= 0) &&
                (ed.selIsHold ? (ed.selected < (int)ed.scene.holds.size())
                              : (ed.selected < (int)ed.scene.boxes.size()));
            if (canDrop) {
                if (ImGui::Button("Drop to Ground")) {
                    const HeightField& hfDrop = (ed.mode == EditorMode::Play && ed.pie.active())
                                                ? ed.pie.hf : ed.editHf;
                    if (!ed.selIsHold) {
                        auto& b = ed.scene.boxes[(size_t)ed.selected];
                        b.pos[1] = hfDrop.heightAt(b.pos[0], b.pos[2]) + b.half[1] + 0.05f;
                        snprintf(ed.statusMsg, sizeof(ed.statusMsg), "Box %d dropped to ground", ed.selected);
                    } else {
                        auto& h = ed.scene.holds[(size_t)ed.selected];
                        h.pos[1] = hfDrop.heightAt(h.pos[0], h.pos[2]) + 0.9f;
                        snprintf(ed.statusMsg, sizeof(ed.statusMsg), "Hold %d dropped to ground", ed.selected);
                    }
                    if (ed.mode == EditorMode::Play) ed.pie.rebuildFromScene(ed.scene);
                }
                ImGui::SameLine();
            }
            if (ImGui::Button("Apply to Running Game") && ed.mode == EditorMode::Play) {
                ed.pie.rebuildFromScene(ed.scene);
                snprintf(ed.statusMsg, sizeof(ed.statusMsg), "Hot-applied to PIE (no restart needed)");
            }
            ImGui::End();
        }

        // ---- Viewport (right, large) ----
        ImGui::SetNextWindowPos(ImVec2(324, 32), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(1020, 715), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Viewport")) {
            ImGui::Text("RMB-drag: orbit | wheel: zoom | LMB-click: select & fly to");

            // ---- click-to-select (screen-space picking) ----
            // Project each object into the viewport and pick the one closest to
            // the mouse. Screen-space is more reliable than ray-AABB for the
            // small cubes this editor deals with.
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImVec2 cpos = ImGui::GetCursorScreenPos();
                ImVec2 csz  = ImGui::GetContentRegionAvail();
                ImVec2 mouse = ImGui::GetMousePos();
                float best = 34.f * 34.f;   // 34px pick radius, squared
                int bestBox = -1, bestHold = -1;

                auto project = [&](const Vec3& wp, float& ox, float& oy, bool& infront) {
                    float cx = vp[0]*wp.x + vp[4]*wp.y + vp[8]*wp.z  + vp[12];
                    float cy = vp[1]*wp.x + vp[5]*wp.y + vp[9]*wp.z  + vp[13];
                    float cw = vp[3]*wp.x + vp[7]*wp.y + vp[11]*wp.z + vp[15];
                    infront = (cw > 0.0001f);
                    if (!infront) return;
                    float ndcX = cx / cw, ndcY = cy / cw;
                    ox = cpos.x + (ndcX * 0.5f + 0.5f) * csz.x;
                    oy = cpos.y + (-ndcY * 0.5f + 0.5f) * csz.y;
                };

                const auto& boxList = *boxesForPick;
                for (size_t i = 0; i < boxList.size(); ++i) {
                    float ox, oy; bool ok;
                    project(boxList[i].pos, ox, oy, ok);
                    if (!ok) continue;
                    float dx = ox - mouse.x, dy = oy - mouse.y;
                    float d2 = dx*dx + dy*dy;
                    if (d2 < best) { best = d2; bestBox = (int)i; bestHold = -1; }
                }
                for (size_t i = 0; i < ed.scene.holds.size(); ++i) {
                    Vec3 p{ ed.scene.holds[i].pos[0], ed.scene.holds[i].pos[1], ed.scene.holds[i].pos[2] };
                    float ox, oy; bool ok;
                    project(p, ox, oy, ok);
                    if (!ok) continue;
                    float dx = ox - mouse.x, dy = oy - mouse.y;
                    float d2 = dx*dx + dy*dy;
                    if (d2 < best) { best = d2; bestHold = (int)i; bestBox = -1; }
                }

                if (bestBox >= 0) {
                    ed.selected = bestBox; ed.selIsHold = false;
                    const BoxProp& b = boxList[(size_t)bestBox];
                    // fly to a comfortable viewing distance from the object
                    Vec3 dir = normalize(ed.cam.pos - b.pos);
                    if (length(dir) < 0.001f) dir = Vec3{0, 0.3f, 1.f};
                    float dist = std::max(6.f, (b.hx + b.hy + b.hz) * 4.f);
                    Vec3 dest = b.pos + dir * dist;
                    float yaw = atan2f(-dir.x, -dir.z);
                    float pitch = asinf(std::max(-1.f, std::min(1.f, -dir.y)));
                    ed.camFollowPlayer = false;
                    ed.flyTo(dest, yaw, pitch);
                    snprintf(ed.statusMsg, sizeof(ed.statusMsg), "Selected Box %d", bestBox);
                } else if (bestHold >= 0) {
                    ed.selected = bestHold; ed.selIsHold = true;
                    Vec3 p{ ed.scene.holds[bestHold].pos[0],
                            ed.scene.holds[bestHold].pos[1],
                            ed.scene.holds[bestHold].pos[2] };
                    Vec3 dir = normalize(ed.cam.pos - p);
                    if (length(dir) < 0.001f) dir = Vec3{0, 0.3f, 1.f};
                    Vec3 dest = p + dir * 8.f;
                    float yaw = atan2f(-dir.x, -dir.z);
                    float pitch = asinf(std::max(-1.f, std::min(1.f, -dir.y)));
                    ed.camFollowPlayer = false;
                    ed.flyTo(dest, yaw, pitch);
                    snprintf(ed.statusMsg, sizeof(ed.statusMsg), "Selected Hold %d", bestHold);
                }
            }
            ImGui::End();
        }

        // ---- camera: orbit (RMB) + zoom, or follow the player in PIE ----
        const float dtFrame = (float)kFixedDt;
        if (ed.camFollowPlayer && ed.pie.active()) {
            // Play mode: trail behind the player, using the same chase scheme
            // as the real game (back along the view direction + head offset).
            Vec3 target = ed.pie.player.pelvisPos();
            Vec3 back{ -sinf(ed.cam.yaw)*cosf(ed.cam.pitch), sinf(ed.cam.pitch),
                        -cosf(ed.cam.yaw)*cosf(ed.cam.pitch) };
            Vec3 want = target + back * 6.5f + Vec3{0, 1.6f, 0};
            float groundClear = ed.pie.hf.heightAt(want.x, want.z) + 0.8f;
            if (want.y < groundClear) want.y = groundClear;
            // while the intro flight runs, feed the chase target into it;
            // afterwards the camera tracks the player directly
            if (ed.camFly.flying()) ed.camFly.retarget(want);
            else                    ed.cam.pos = want;
        } else if (input.mouseButtons[1]) {
            ed.cam.yaw   += input.mouseDX * 0.005f;
            ed.cam.pitch  = std::max(-1.3f, std::min(1.4f, ed.cam.pitch - input.mouseDY * 0.005f));
        }
        if (input.mouseWheel != 0) {
            Vec3 fwd{ sinf(ed.cam.yaw)*cosf(ed.cam.pitch), -sinf(ed.cam.pitch), cosf(ed.cam.yaw)*cosf(ed.cam.pitch) };
            ed.cam.pos = ed.cam.pos + fwd * (float)input.mouseWheel * 3.f;
        }
        // (camera follow + view matrix + box list are computed earlier, above
        //  the ImGui panels, because viewport picking needs them)

        // ---- PIE stepping (with input so WASD works in the editor) ----
        if (ed.mode == EditorMode::Play) {
            acc += kFixedDt;
            int guard = 0;
            while (acc >= kFixedDt && guard++ < 4) {
                ed.pie.step((float)kFixedDt, &input, ed.cam.yaw);
                acc -= kFixedDt;
            }
        }

        // ---- render (vp already computed above, before the UI panels) ----
        ID3D11DeviceContext* ctx = gfx.ctx();
        ctx->UpdateSubresource(cbFrame, 0, nullptr, vp, 0, 0);

        gfx.beginFrame(0.45f, 0.62f, 0.85f);
        ctx->IASetInputLayout(il);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->PSSetShader(ps, nullptr, 0);
        ID3D11Buffer* cbs[3] = { cbFrame, cbObj, cbTint };
        ctx->VSSetConstantBuffers(0, 3, cbs);

        float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        float white[4] = {1,1,1,1};
        ctx->UpdateSubresource(cbObj, 0, nullptr, identity, 0, 0);
        ctx->UpdateSubresource(cbTint, 0, nullptr, white, 0, 0);
        mountain.draw(ctx);

        // draw scene boxes (Edit) or live PIE boxes (Play)
        for (size_t i = 0; i < boxesForPick->size(); ++i) {
            const BoxProp& b = (*boxesForPick)[i];
            Vec3 ax0 = rotate(b.rot, Vec3{1,0,0});
            Vec3 ax1 = rotate(b.rot, Vec3{0,1,0});
            Vec3 ax2 = rotate(b.rot, Vec3{0,0,1});
            float sx = b.hx*2.f, sy = b.hy*2.f, sz = b.hz*2.f;
            float w[16] = {
                ax0.x*sx, ax0.y*sx, ax0.z*sx, 0,
                ax1.x*sy, ax1.y*sy, ax1.z*sy, 0,
                ax2.x*sz, ax2.y*sz, ax2.z*sz, 0,
                b.pos.x, b.pos.y, b.pos.z, 1
            };
            ctx->UpdateSubresource(cbObj, 0, nullptr, w, 0, 0);
            bool sel = (!ed.selIsHold && ed.selected == (int)i);
            float col[4] = { sel ? 1.f : 0.72f, sel ? 0.85f : 0.55f, sel ? 0.3f : 0.34f, 1 };
            ctx->UpdateSubresource(cbTint, 0, nullptr, col, 0, 0);
            unitMesh.draw(ctx);
        }

        // ---- holds (orange cubes, bigger for checkpoints) ----
        const auto& holds = (ed.mode == EditorMode::Play) ? ed.pie.route.holds()
                                                          : authoredHolds(ed);
        for (size_t i = 0; i < holds.size(); ++i) {
            const Hold& h = holds[i];
            float s = h.checkpoint ? 0.5f : 0.22f;
            bool isSel = (ed.selIsHold && ed.selected == (int)i);
            float tint[4] = { isSel ? 1.f : (h.checkpoint ? 0.95f : 0.95f),
                              isSel ? 0.9f : (h.checkpoint ? 0.75f : 0.55f),
                              isSel ? 0.3f : (h.checkpoint ? 0.10f : 0.15f), 1.f };
            float hw[16] = { s,0,0,0, 0,s,0,0, 0,0,s,0,
                             h.pos.x, h.pos.y, h.pos.z, 1 };
            ctx->UpdateSubresource(cbObj, 0, nullptr, hw, 0, 0);
            ctx->UpdateSubresource(cbTint, 0, nullptr, tint, 0, 0);
            unitMesh.draw(ctx);
        }

        // ---- climbers (in Play mode): local player + remote buddy ----
        if (ed.mode == EditorMode::Play && ed.pie.active()) {
            drawClimber(ctx, cbObj, cbTint, unitMesh, ed.pie.player, false);
            if (ed.pie.buddyActive)
                drawClimber(ctx, cbObj, cbTint, unitMesh, ed.pie.buddy, true);
        }

        // UI overlay: depth OFF so ImGui always draws on top (same as the
        // game's beginUI() path for its HUD).
        gfx.beginUI();

        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        gfx.endFrame();
        input.endFrame();
    }

    if (ed.pie.active()) ed.pie.stop();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    unitMesh.shutdown(); mountain.shutdown(); gfx.shutdown();
    return 0;
}
