// bipbip — «БИП БИП НА ГОРУ»
// Phase 2.2-2.4: verlet ragdoll climber, WASD + jump + two-hand grabbing,
// chase camera. Boxes tumble around him.
#include "platform/window_win32.h"
#include "render/dx11_device.h"
#include "render/shader_manager.h"
#include "render/mesh.h"
#include "render/camera.h"
#include "world/heightfield.h"
#include "physics/verlet.h"
#include "game/player/climber.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace bip;

int main(int argc, char** argv) {
    uint64_t seed = (argc > 1) ? strtoull(argv[1], nullptr, 10) : 1337ull;

    Window window;
    if (!window.create("BIP BIP NA GORU", 1280, 720)) { fprintf(stderr, "window failed\n"); return 1; }
    Dx11Device gfx;
    if (!gfx.init(window.handle(), 1280, 720)) { fprintf(stderr, "d3d11 failed\n"); return 2; }

    ShaderManager shaders;
    if (!shaders.init(gfx.device())) return 3;

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex,pos),    D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex,normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(Vertex,uv),      D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex,color),   D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    ID3D11VertexShader* vs = nullptr; ID3D11InputLayout* il = nullptr; ID3D11PixelShader* ps = nullptr;
    std::string errs;
    if (!shaders.loadVertexShader("assets/shaders/basic_vs.hlsl", "main", layout, 4, &vs, &il, &errs)) {
        fprintf(stderr, "VS error:\n%s\n", errs.c_str()); return 4;
    }
    if (!shaders.loadPixelShader("assets/shaders/basic_ps.hlsl", "main", &ps, &errs)) {
        fprintf(stderr, "PS error:\n%s\n", errs.c_str()); return 5;
    }

    // --- world ---------------------------------------------------------------
    HeightField hf;
    hf.generate(seed, 300.f, 256);
    Mesh mountain;
    {
        const auto& vv = hf.vertices();
        const auto& ii = hf.indices();
        if (!mountain.init(gfx.device(), vv.data(), (uint32_t)vv.size(),
                           ii.data(), (uint32_t)ii.size())) return 6;
    }

    VerletWorld phys;
    phys.init(&hf);

    // crate mesh (tinted at draw time)
    auto boxVerts = geom::box(1.f, 1.f, 1.f);
    auto boxIdx = geom::boxIndices();
    Mesh unitMesh;
    if (!unitMesh.init(gfx.device(), boxVerts.data(), (uint32_t)boxVerts.size(),
                       boxIdx.data(), (uint32_t)boxIdx.size())) return 8;

    for (int i = 0; i < 14; ++i) {
        Rng rng(seed ^ (0xC0FFEE + i));
        float a = rng.unit() * 6.2831853f;
        float r = 20.f + rng.unit() * 60.f;
        float x = cosf(a) * r, z = sinf(a) * r;
        float y = hf.heightAt(x, z) + 2.f + rng.unit() * 15.f;
        float s = 1.2f + rng.unit() * 1.2f;
        phys.addBox(Vec3{x, y, z}, s*0.5f, s*0.5f, s*0.5f);
    }

    // the boi
    Climber player;
    player.init(&phys, &hf, Vec3{0, 0, -80});
    Climber::PartBox parts[32];

    // constant buffers
    struct CBPerFrame { float viewProj[16]; };
    struct CBPerObject { float world[16]; };
    struct CBPerTint  { float tint[4]; };
    ID3D11Buffer* cbFrame = nullptr; ID3D11Buffer* cbObj = nullptr; ID3D11Buffer* cbTint = nullptr;
    D3D11_BUFFER_DESC cbd{}; cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.ByteWidth = sizeof(CBPerFrame); cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    gfx.device()->CreateBuffer(&cbd, nullptr, &cbFrame);
    cbd.ByteWidth = sizeof(CBPerObject);
    gfx.device()->CreateBuffer(&cbd, nullptr, &cbObj);
    cbd.ByteWidth = sizeof(CBPerTint);
    gfx.device()->CreateBuffer(&cbd, nullptr, &cbTint);

    float identity[16] = { 1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1 };
    float whiteTint[4] = { 1,1,1,1 };

    InputState input;
    uint64_t frame = 0;
    double simTime = 0;
    const double kFixedDt = 1.0 / 60.0;
    double acc = 0;
    Camera chaseCam;                       // yaw/pitch driven by mouse, pos by player
    chaseCam.pos = Vec3{0, 5, -90};

    while (!window.shouldClose()) {
        window.pumpMessages(input);
        if (input.down(VK_ESCAPE)) break;

        RECT rc; GetClientRect(window.handle(), &rc);
        if (rc.right > 0 && rc.bottom > 0) gfx.resize(rc.right, rc.bottom);
        float aspect = (float)rc.right / (float)rc.bottom;

        acc += 1.0 / 60.0;
        int steps = 0;
        while (acc >= kFixedDt && steps < 4) {
            // movement basis from camera yaw
            float cy = chaseCam.yaw;
            Vec3 f{ sinf(cy), 0, cosf(cy) };
            Vec3 r{ cosf(cy), 0, -sinf(cy) };
            Vec3 move{};
            if (input.down('W')) move = move + f;
            if (input.down('S')) move = move - f;
            if (input.down('A')) move = move - r;
            if (input.down('D')) move = move + r;
            bool wantJump = input.down(VK_SPACE);

            // grab targets: points in front of pelvis at chest height
            Vec3 pelvis = player.pelvisPos();
            Vec3 lookF{ sinf(chaseCam.yaw), 0.f, cosf(chaseCam.yaw) };
            Vec3 grabPtL = pelvis + lookF * 0.9f + Vec3{-0.35f, 0.35f, 0};
            Vec3 grabPtR = pelvis + lookF * 0.9f + Vec3{ 0.35f, 0.35f, 0};
            bool grabL = input.mouseButtons[0];
            bool grabR = input.mouseButtons[1];

            player.control(move, wantJump, grabL, grabR, grabPtL, grabPtR);

            phys.step((float)kFixedDt);
            player.simulate((float)kFixedDt);
            acc -= kFixedDt; simTime += kFixedDt; ++steps;
        }

        // ---- chase camera
        Vec3 target = player.pelvisPos();
        chaseCam.yaw += input.mouseDX * 0.003f;
        chaseCam.pitch = std::max(-1.2f, std::min(1.35f, chaseCam.pitch + input.mouseDY * 0.003f));
        Vec3 back{ -sinf(chaseCam.yaw)*cosf(chaseCam.pitch), sinf(chaseCam.pitch),
                    -cosf(chaseCam.yaw)*cosf(chaseCam.pitch) };
        Vec3 camWant = target + back * 6.5f + Vec3{0, 1.6f, 0};
        float groundClear = hf.heightAt(camWant.x, camWant.z) + 0.8f;
        if (camWant.y < groundClear) camWant.y = groundClear;
        chaseCam.pos = camWant;

        float vp[16];
        chaseCam.viewProj(vp, aspect);

        ID3D11DeviceContext* ctx = gfx.ctx();
        ctx->UpdateSubresource(cbFrame, 0, nullptr, vp, 0, 0);

        gfx.beginFrame(0.45f, 0.65f, 0.90f);
        ctx->IASetInputLayout(il);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->PSSetShader(ps, nullptr, 0);
        ID3D11Buffer* cbsAll[3] = { cbFrame, cbObj, cbTint };
        ctx->VSSetConstantBuffers(0, 3, cbsAll);

        ctx->UpdateSubresource(cbObj, 0, nullptr, identity, 0, 0);
        ctx->UpdateSubresource(cbTint, 0, nullptr, whiteTint, 0, 0);
        mountain.draw(ctx);

        // crates: brownish, full quaternion orientation
        for (auto& b : phys.boxes_) {
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
            float brown[4] = { 0.72f, 0.55f, 0.34f, 1 };
            ctx->UpdateSubresource(cbTint, 0, nullptr, brown, 0, 0);
            unitMesh.draw(ctx);
        }
        // player body parts: oriented boxes (bone-aligned, joint cubes seal gaps)
        int np = player.collectParts(parts);
        for (int i = 0; i < np; ++i) {
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
            float col[4] = { pb.color.x, pb.color.y, pb.color.z, 1 };
            ctx->UpdateSubresource(cbTint, 0, nullptr, col, 0, 0);
            unitMesh.draw(ctx);
        }
        gfx.endFrame();

        input.endFrame();
        ++frame;
    }

    player.shutdown(&phys);
    unitMesh.shutdown(); mountain.shutdown(); gfx.shutdown();
    printf("clean exit after %llu frames\n", (unsigned long long)frame);
    return 0;
}
