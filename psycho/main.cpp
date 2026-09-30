// psycho — the second game on the bipbip engine.
//
// psycho shares NOTHING with the climbing game except the engine libraries:
// it does not include Route, RunState, the climbing HUD, the menu, or the
// climbing config. What it uses is exactly the reusable surface that the
// engine generalisation was for:
//
//   engine/character/climber  — the ragdoll rig, with terrain and box collision
//   engine/physics/verlet     — fixed-step solver + rigid boxes
//   engine/world/heightfield  — procedural ground
//   engine/core/scene         — typed entities, .bipscene v2 round-trip
//   engine/core/game_config   — EngineConfig (shared) + GameplayConfig (psycho's)
//   engine/render/*           — DX11, shaders, meshes, camera, frustum
//
// The world: a flat-ish procedural floor with a stack of boxes and a player
// that walks into them. Deliberately the smallest thing that is still a game,
// so anything missing from the engine shows up immediately.
#include "platform/window_win32.h"
#include "render/dx11_device.h"
#include "render/shader_manager.h"
#include "render/mesh.h"
#include "render/camera.h"
#include "render/frustum.h"
#include "world/heightfield.h"
#include "physics/verlet.h"
#include "character/climber.h"
#include "character/move_basis.h"
#include "core/scene.h"
#include "core/game_config.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <windows.h>
#include <io.h>

using namespace bip;

namespace {

// projectRoot() walks up from the exe to the folder containing /assets, so the
// game runs from build/psycho/ as well as from the repo root.
std::string projectRoot() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p(buf, n);
    for (int up = 0; up < 6; ++up) {
        if (p.empty()) break;
        const char slash = p.back();
        p.pop_back();
        while (!p.empty() && p.back() != '\\' && p.back() != '/') p.pop_back();
        (void)slash;
        if (p.empty()) break;
        std::string probe = p + "assets";
        if (_access((probe + "/shaders/basic_vs.hlsl").c_str(), 0) == 0) {
            if (!p.empty() && p.back() != '\\' && p.back() != '/') p += "/";
            return p;
        }
    }
    return "./";
}

} // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    fprintf(stderr, "[psycho] boot\n");

    const std::string assetRoot = projectRoot();
    fprintf(stderr, "[psycho] assetRoot=%s\n", assetRoot.c_str());

    // psycho's own config: the engine half is shared, the gameplay half is ours.
    // A missing file is written out, so there is always something to edit.
    EngineConfig eng;
    eng.reset();
    if (!eng.load(assetRoot + "assets/config/psycho_engine.cfg")) {
        CreateDirectoryA((assetRoot + "assets").c_str(), nullptr);
        CreateDirectoryA((assetRoot + "assets/config").c_str(), nullptr);
        eng.save(assetRoot + "assets/config/psycho_engine.cfg");
        fprintf(stderr, "[psycho] wrote default engine config\n");
    }
    GameplayConfig gameplay;
    gameplay.reset();
    gameplay.load(assetRoot + "assets/config/psycho_gameplay.cfg");

    // psycho's world, authored as a scene so the editor and the test share it.
    const std::string scenePath = assetRoot + "assets/scenes/psycho.bipscene";
    Scene scene;
    if (!loadSceneV2(scenePath, scene)) {
        // No authored scene yet: build a small one in code and save it, so the
        // file exists for the editor to open.
        fprintf(stderr, "[psycho] no scene, authoring a starter one\n");
        scene.seed = eng.world.seed;
        scene.spawn[0] = 0.f; scene.spawn[1] = 0.f; scene.spawn[2] = -6.f;

        // a stack of boxes in front of the spawn
        for (int i = 0; i < 5; ++i) {
            Entity b;
            b.id = (uint32_t)(i + 1);
            b.type = kTypeBox;
            b.pos[0] = 4.f; b.pos[1] = 1.f + (float)i * 1.6f; b.pos[2] = 2.f;
            b.rotDeg[1] = (float)i * 12.f;
            b.box = BoxComp{};
            b.box->half[0] = 1.f; b.box->half[1] = 0.8f; b.box->half[2] = 1.f;
            b.tag = "stack";
            scene.entities.push_back(b);
        }
        // a wall to walk into
        Entity wall;
        wall.id = 100;
        wall.type = kTypeBox;
        wall.pos[0] = 10.f; wall.pos[1] = 2.f; wall.pos[2] = 0.f;
        wall.box = BoxComp{};
        wall.box->half[0] = 0.5f; wall.box->half[1] = 2.f; wall.box->half[2] = 6.f;
        wall.tag = "wall";
        scene.entities.push_back(wall);
        // a lazer, a type the climbing game has no concept of
        Entity laz;
        laz.id = 200;
        laz.type = kTypeLazer;
        laz.pos[0] = -4.f; laz.pos[1] = 3.f; laz.pos[2] = 0.f;
        laz.lazer = LazerComp{};
        laz.lazer->color[0] = 1.f; laz.lazer->color[1] = 0.15f; laz.lazer->color[2] = 0.15f;
        laz.lazer->damage = 2;
        laz.tag = "hazard";
        scene.entities.push_back(laz);

        saveSceneV2(scene, scenePath);
    }
    scene.syncView();   // entities -> boxes/holds, the form the sim consumes
    fprintf(stderr, "[psycho] scene: %zu entities, %zu boxes\n",
            scene.entities.size(), scene.boxes.size());

    // ---- window + d3d11 ---------------------------------------------------
    Window window;
    if (!window.create("psycho", 1280, 720)) { fprintf(stderr, "window failed\n"); return 1; }
    Dx11Device gfx;
    if (!gfx.init(window.handle(), 1280, 720)) { fprintf(stderr, "d3d11 failed\n"); return 2; }

    ShaderManager shaders;
    if (!shaders.init(gfx.device())) { fprintf(stderr, "shader mgr failed\n"); return 3; }

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex,pos),    D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex,normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(Vertex,uv),      D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex,color),   D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    ID3D11VertexShader* vs = nullptr;
    ID3D11InputLayout* il = nullptr;
    ID3D11PixelShader* ps = nullptr;
    std::string errs;
    if (!shaders.loadVertexShader(assetRoot + "assets/shaders/basic_vs.hlsl", "main",
                                  layout, 4, &vs, &il, &errs)) {
        fprintf(stderr, "VS error:\n%s\n", errs.c_str()); return 4;
    }
    if (!shaders.loadPixelShader(assetRoot + "assets/shaders/basic_ps.hlsl", "main", &ps, &errs)) {
        fprintf(stderr, "PS error:\n%s\n", errs.c_str()); return 5;
    }

    // ---- world + physics ---------------------------------------------------
    HeightField hf;
    hf.generate(scene.seed, eng.world.worldSize, eng.world.heightN);
    Mesh ground;
    {
        const auto& vv = hf.vertices();
        const auto& ii = hf.indices();
        if (!ground.init(gfx.device(), vv.data(), (uint32_t)vv.size(),
                         ii.data(), (uint32_t)ii.size())) return 6;
    }

    VerletWorld phys;
    phys.init(&hf);
    for (const auto& b : scene.boxes) {
        // scene boxes are authored in world space; sit them on the terrain
        phys.addBox(Vec3{b.pos[0], b.pos[1], b.pos[2]}, b.half[0], b.half[1], b.half[2]);
    }
    fprintf(stderr, "[psycho] physics: %zu boxes\n", phys.boxes_.size());

    // ---- the character, straight from the engine ---------------------------
    Climber player;
    player.init(&phys, &hf,
                Vec3{scene.spawn[0], 0.f, scene.spawn[2]});
    fprintf(stderr, "[psycho] player pelvis=(%.2f, %.2f, %.2f)\n",
            player.pelvisPos().x, player.pelvisPos().y, player.pelvisPos().z);

    // unit cube for boxes and body parts
    Mesh unitMesh;
    {
        auto bv = geom::box(1.f, 1.f, 1.f);
        auto bi = geom::boxIndices();
        if (!unitMesh.init(gfx.device(), bv.data(), (uint32_t)bv.size(),
                           bi.data(), (uint32_t)bi.size())) return 7;
    }

    // ---- constant buffers ---------------------------------------------------
    struct CBPerFrame { float viewProj[16]; };
    struct CBPerObject { float world[16]; };
    struct CBPerTint  { float tint[4]; };
    ID3D11Buffer* cbFrame = nullptr;
    ID3D11Buffer* cbObj = nullptr;
    ID3D11Buffer* cbTint = nullptr;
    D3D11_BUFFER_DESC cbd{};
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.ByteWidth = sizeof(CBPerFrame); gfx.device()->CreateBuffer(&cbd, nullptr, &cbFrame);
    cbd.ByteWidth = sizeof(CBPerObject); gfx.device()->CreateBuffer(&cbd, nullptr, &cbObj);
    cbd.ByteWidth = sizeof(CBPerTint);   gfx.device()->CreateBuffer(&cbd, nullptr, &cbTint);

    float identity[16] = { 1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1 };
    float white[4] = { 1,1,1,1 };

    InputState input;
    uint64_t frame = 0;
    double acc = 0.0;
    const double fixedDt = eng.physics.fixedDt;

    Camera cam;
    cam.pos = Vec3{0, 8, -14};
    cam.yaw = 0.f;
    cam.pitch = 0.25f;

    Frustum culler;
    float planes[6][4] = {};
    Climber::PartBox parts[32];

    float prevForward = 0.f, prevBack = 0.f, prevLeft = 0.f, prevRight = 0.f, prevSpace = 0.f;
    double tPrev = (double)clock() / CLOCKS_PER_SEC;
    int drawCalls = 0;

    window.setRawMouseMode(true);

    while (!window.shouldClose()) {
        window.pumpMessages(input);
        if (input.down(VK_ESCAPE)) break;

        // --- input: WASD drives the character, mouse drives the camera
        const float fwd   = input.down('W') ? 1.f : 0.f;
        const float back  = input.down('S') ? 1.f : 0.f;
        const float left  = input.down('A') ? 1.f : 0.f;
        const float right = input.down('D') ? 1.f : 0.f;
        const bool  jump  = input.down(VK_SPACE);

        // Same convention as the climbing game: yaw follows +mouseDX, pitch
        // follows +mouseDY, both scaled by the configured sensitivity, then the
        // pitch is clamped. The earlier `-mouseDY` here inverted look up/down.
        cam.yaw += (float)input.mouseDX * eng.camera.sensitivity;
        cam.pitch += (float)input.mouseDY * eng.camera.sensitivity;
        if (cam.pitch < eng.camera.pitchMin) cam.pitch = eng.camera.pitchMin;
        if (cam.pitch > eng.camera.pitchMax) cam.pitch = eng.camera.pitchMax;

        // Movement goes through the engine's shared basis. The inline version
        // here had the wrong right vector and ignored `back`/`left` entirely,
        // which is why D steered backwards and S/A did nothing.
        const Vec3 move = moveBasisFor(cam.yaw, fwd, back, left, right);

        // --- fixed-step simulation
        acc += 1.0 / 60.0;
        int steps = 0;
        while (acc >= fixedDt && steps < 5) {
            // The camera decides which way the avatar faces, even while idle.
            // Without this the body keeps its spawn facing and only the camera
            // turns, so the character appears to strafe backwards.
            player.setFacingYaw(cam.yaw);
            player.control(move, jump, false, false, Vec3{}, Vec3{});
            phys.step((float)fixedDt);
            player.simulate((float)fixedDt);
            acc -= fixedDt;
            ++steps;
        }

        // --- chase camera behind the player
        const Vec3 pp = player.pelvisPos();
        Vec3 backv{ -sinf(cam.yaw) * cosf(cam.pitch),
                     sinf(cam.pitch),
                    -cosf(cam.yaw) * cosf(cam.pitch) };
        cam.pos = pp + backv * 6.5f + Vec3{0.f, 1.6f, 0.f};
        const float groundClear = hf.heightAt(cam.pos.x, cam.pos.z) + 1.0f;
        if (cam.pos.y < groundClear) cam.pos.y = groundClear;

        // --- render
        float aspect = 1280.f / 720.f;
        if (window.width() > 0 && window.height() > 0)
            aspect = (float)window.width() / (float)window.height();
        float vp[16];
        cam.viewProj(vp, aspect);
        Camera::frustumPlanes(vp, planes);
        culler.setPlanes(planes);

        ID3D11DeviceContext* ctx = gfx.ctx();
        ctx->UpdateSubresource(cbFrame, 0, nullptr, vp, 0, 0);

        gfx.beginFrame(0.12f, 0.12f, 0.16f);
        ctx->IASetInputLayout(il);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->PSSetShader(ps, nullptr, 0);
        ID3D11Buffer* cbs[3] = { cbFrame, cbObj, cbTint };
        ctx->VSSetConstantBuffers(0, 3, cbs);

        drawCalls = 0;
        ctx->UpdateSubresource(cbObj, 0, nullptr, identity, 0, 0);
        ctx->UpdateSubresource(cbTint, 0, nullptr, white, 0, 0);
        ground.draw(ctx);
        ++drawCalls;

        // boxes, with their full rigid orientation
        for (const auto& b : phys.boxes_) {
            const float rad = sqrtf(b.hx*b.hx + b.hy*b.hy + b.hz*b.hz) * 2.f;
            if (!culler.sphereVisible(b.pos.x, b.pos.y, b.pos.z, rad)) continue;
            const Vec3 ax0 = rotate(b.rot, Vec3{1,0,0});
            const Vec3 ax1 = rotate(b.rot, Vec3{0,1,0});
            const Vec3 ax2 = rotate(b.rot, Vec3{0,0,1});
            const float sx = b.hx*2.f, sy = b.hy*2.f, sz = b.hz*2.f;
            float w[16] = {
                ax0.x*sx, ax0.y*sx, ax0.z*sx, 0,
                ax1.x*sy, ax1.y*sy, ax1.z*sy, 0,
                ax2.x*sz, ax2.y*sz, ax2.z*sz, 0,
                b.pos.x,  b.pos.y,  b.pos.z,  1
            };
            ctx->UpdateSubresource(cbObj, 0, nullptr, w, 0, 0);
            const float tint[4] = { 0.55f, 0.58f, 0.65f, 1.f };
            ctx->UpdateSubresource(cbTint, 0, nullptr, tint, 0, 0);
            unitMesh.draw(ctx);
            ++drawCalls;
        }

        // lazer posts: a thin tall box at the lazer entity's position
        for (const auto& e : scene.entities) {
            if (e.type != kTypeLazer) continue;
            const float rad = 1.5f;
            if (!culler.sphereVisible(e.pos[0], e.pos[1], e.pos[2], rad)) continue;
            float w[16] = { 0.12f,0,0,0,  0,2.4f,0,0,  0,0,0.12f,0,
                            e.pos[0], e.pos[1], e.pos[2], 1 };
            ctx->UpdateSubresource(cbObj, 0, nullptr, w, 0, 0);
            float tint[4] = { 1.f, 0.15f, 0.15f, 1.f };
            if (e.lazer) {
                tint[0] = e.lazer->color[0];
                tint[1] = e.lazer->color[1];
                tint[2] = e.lazer->color[2];
            }
            ctx->UpdateSubresource(cbTint, 0, nullptr, tint, 0, 0);
            unitMesh.draw(ctx);
            ++drawCalls;
        }

        // the character, from the engine rig
        if (culler.sphereVisible(pp.x, pp.y, pp.z, 3.0f)) {
            const int np = player.collectParts(parts);
            for (int i = 0; i < np; ++i) {
                const auto& pb = parts[i];
                const Vec3 y = pb.yAxis;
                const Vec3 x = normalize(cross(y, pb.zHint));
                const Vec3 z = cross(x, y);
                const float sx = pb.half.x*2.f, sy = pb.half.y*2.f, sz = pb.half.z*2.f;
                float w[16] = {
                    x.x*sx, x.y*sx, x.z*sx, 0,
                    y.x*sy, y.y*sy, y.z*sy, 0,
                    z.x*sz, z.y*sz, z.z*sz, 0,
                    pb.center.x, pb.center.y, pb.center.z, 1
                };
                ctx->UpdateSubresource(cbObj, 0, nullptr, w, 0, 0);
                const float col[4] = { pb.color.x, pb.color.y, pb.color.z, 1.f };
                ctx->UpdateSubresource(cbTint, 0, nullptr, col, 0, 0);
                unitMesh.draw(ctx);
                ++drawCalls;
            }
        }

        gfx.endFrame();
        ++frame;

        // Clear per-frame accumulators. Without this, mouseDX/mouseDY keep
        // every delta ever seen and each frame re-adds the last one, so the
        // camera spins uncontrollably from a single small mouse move.
        input.endFrame();

        if (frame % 120 == 0) {
            const double now = (double)clock() / CLOCKS_PER_SEC;
            const double ms = (now - tPrev) * 1000.0 / 120.0;
            tPrev = now;
            fprintf(stderr, "[psycho] frame %llu  %.2f ms  draws %d  pelvis=(%.2f, %.2f, %.2f)\n",
                    (unsigned long long)frame, ms, drawCalls,
                    pp.x, pp.y, pp.z);
        }

        prevForward = fwd; prevBack = back; prevLeft = left; prevRight = right; prevSpace = (float)jump;
        (void)prevForward; (void)prevBack; (void)prevLeft; (void)prevRight; (void)prevSpace;
    }

    window.setRawMouseMode(false);
    fprintf(stderr, "[psycho] done after %llu frames\n", (unsigned long long)frame);
    return 0;
}
