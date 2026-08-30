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
};

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
        HeightField tmp; tmp.generate(ed.scene.seed, ed.scene.worldSize, ed.scene.heightN);
        mountain.init(gfx.device(), tmp.vertices().data(), (uint32_t)tmp.vertices().size(),
                      tmp.indices().data(), (uint32_t)tmp.indices().size());
    }
    {
        auto vv = geom::box(1.f, 1.f, 1.f);
        auto ii = geom::boxIndices();
        unitMesh.init(gfx.device(), vv.data(), (uint32_t)vv.size(), ii.data(), (uint32_t)ii.size());
    }

    TextRenderer hud;
    hud.init(gfx.device(), 1600, 900);

    ed.cam.pos = Vec3{0, 20, -60};
    ed.cam.pitch = 0.35f;

    InputState input;
    const double kFixedDt = 1.0 / 60.0;
    double acc = 0;

    while (!window.shouldClose()) {
        window.pumpMessages(input);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // ---- docking root ----
        ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

        // ---- Toolbar ----
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
                if (ImGui::Button("| Stop")) { ed.pie.stop(); ed.mode = EditorMode::Edit; }
            } else {
                if (ImGui::Button("> Play")) {
                    ed.pie.start(ed.scene, gfx.device());
                    ed.mode = EditorMode::Play;
                }
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.16f,0.75f,0.62f,1.f), playing ? "PLAYING (PIE)" : "EDIT");
            ImGui::SameLine();
            ImGui::Text("| %s", ed.statusMsg);
            ImGui::EndMainMenuBar();
        }

        // ---- Outliner ----
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
                if (ImGui::Selectable(lbl, (!ed.selIsHold && ed.selected == (int)i))) {
                    ed.selected = (int)i; ed.selIsHold = false;
                }
            }
            ImGui::Text("Holds (%zu)", ed.scene.holds.size());
            for (size_t i = 0; i < ed.scene.holds.size(); ++i) {
                char lbl[64]; snprintf(lbl, sizeof(lbl), "Hold %zu%s", i, ed.scene.holds[i].checkpoint ? " [CP]" : "");
                if (ImGui::Selectable(lbl, (ed.selIsHold && ed.selected == (int)i))) {
                    ed.selected = (int)i; ed.selIsHold = true;
                }
            }
            ImGui::End();
        }

        // ---- Details ----
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
            if (ImGui::Button("Apply to Running Game") && ed.mode == EditorMode::Play) {
                ed.pie.rebuildFromScene(ed.scene);
                snprintf(ed.statusMsg, sizeof(ed.statusMsg), "Hot-applied to PIE (no restart needed)");
            }
            ImGui::End();
        }

        // ---- Viewport ----
        if (ImGui::Begin("Viewport")) {
            ImVec2 sz = ImGui::GetContentRegionAvail();
            ImGui::Text("size %.0fx%.0f  |  RMB-drag: orbit  |  wheel: zoom", sz.x, sz.y);
            ImGui::End();
        }

        // camera orbit (RMB) + zoom
        if (input.mouseButtons[1]) {
            ed.cam.yaw   += input.mouseDX * 0.005f;
            ed.cam.pitch  = std::max(-1.3f, std::min(1.4f, ed.cam.pitch - input.mouseDY * 0.005f));
        }
        if (input.mouseWheel != 0) {
            Vec3 fwd{ sinf(ed.cam.yaw)*cosf(ed.cam.pitch), -sinf(ed.cam.pitch), cosf(ed.cam.yaw)*cosf(ed.cam.pitch) };
            ed.cam.pos = ed.cam.pos + fwd * (float)input.mouseWheel * 3.f;
        }

        // PIE stepping
        if (ed.mode == EditorMode::Play) {
            acc += kFixedDt;
            int guard = 0;
            while (acc >= kFixedDt && guard++ < 4) { ed.pie.step((float)kFixedDt); acc -= kFixedDt; }
        }

        // ---- render ----
        float aspect = 1600.f / 900.f;
        float vp[16];
        ed.cam.viewProj(vp, aspect);

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
        const std::vector<BoxProp>* drawBoxes = nullptr;
        std::vector<BoxProp> editBoxes;
        if (ed.mode == EditorMode::Play) {
            drawBoxes = &ed.pie.phys.boxes_;
        } else {
            for (const auto& sb : ed.scene.boxes) {
                BoxProp b{};
                b.pos = Vec3{sb.pos[0], sb.pos[1], sb.pos[2]};
                b.hx = sb.half[0]; b.hy = sb.half[1]; b.hz = sb.half[2];
                editBoxes.push_back(b);
            }
            drawBoxes = &editBoxes;
        }
        for (size_t i = 0; i < drawBoxes->size(); ++i) {
            const BoxProp& b = (*drawBoxes)[i];
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
