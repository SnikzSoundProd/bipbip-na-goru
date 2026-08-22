// bipbip — «БИП БИП НА ГОРУ»
// Phase 2 (custom verlet physics): boxes tumble down the mountain.
#include "platform/window_win32.h"
#include "render/dx11_device.h"
#include "render/shader_manager.h"
#include "render/mesh.h"
#include "render/camera.h"
#include "world/heightfield.h"
#include "physics/verlet.h"
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
    fprintf(stderr, "[chk] terrain gen...\n"); fflush(stderr);
    HeightField hf;
    hf.generate(seed, 300.f, 256);
    Mesh mountain;
    {
        const auto& vv = hf.vertices();
        const auto& ii = hf.indices();
        if (!mountain.init(gfx.device(), vv.data(), (uint32_t)vv.size(),
                           ii.data(), (uint32_t)ii.size())) return 6;
    }
    fprintf(stderr, "[chk] terrain ready\n"); fflush(stderr);

    VerletWorld phys;
    phys.init(&hf);

    auto boxVerts = geom::box(1.f, 1.f, 1.f);
    auto boxIdx = geom::boxIndices();
    Mesh crateMesh;
    if (!crateMesh.init(gfx.device(), boxVerts.data(), (uint32_t)boxVerts.size(),
                        boxIdx.data(), (uint32_t)boxIdx.size())) return 8;

    // crates scattered over the slopes
    Rng rng(seed ^ 0xC0FFEE);
    for (int i = 0; i < 14; ++i) {
        float a = rng.unit() * 6.2831853f;
        float r = 20.f + rng.unit() * 60.f;
        float x = cosf(a) * r, z = sinf(a) * r;
        float y = hf.heightAt(x, z) + 2.f + rng.unit() * 20.f;
        float s = 1.2f + rng.unit() * 1.5f;
        phys.addBox(Vec3{x, y, z}, s*0.5f, s*0.5f, s*0.5f);
    }
    fprintf(stderr, "[chk] %d crates, entering loop\n", (int)phys.boxes_.size()); fflush(stderr);

    // constant buffers
    struct CBPerFrame { float viewProj[16]; };
    struct CBPerObject { float world[16]; };
    ID3D11Buffer* cbFrame = nullptr; ID3D11Buffer* cbObj = nullptr;
    D3D11_BUFFER_DESC cbd{}; cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.ByteWidth = sizeof(CBPerFrame); cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    gfx.device()->CreateBuffer(&cbd, nullptr, &cbFrame);
    cbd.ByteWidth = sizeof(CBPerObject);
    gfx.device()->CreateBuffer(&cbd, nullptr, &cbObj);

    float identity[16] = { 1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1 };

    InputState input;
    bool prevKeys[256] = {};
    uint64_t frame = 0;
    double simTime = 0;
    const double kFixedDt = 1.0 / 60.0;
    double acc = 0;

    while (!window.shouldClose()) {
        window.pumpMessages(input);
        if (input.down(VK_ESCAPE)) break;

        RECT rc; GetClientRect(window.handle(), &rc);
        if (rc.right > 0 && rc.bottom > 0) gfx.resize(rc.right, rc.bottom);
        float aspect = (float)rc.right / (float)rc.bottom;

        acc += 1.0 / 60.0;
        int steps = 0;
        while (acc >= kFixedDt && steps < 4) {
            phys.step((float)kFixedDt);
            acc -= kFixedDt; simTime += kFixedDt; ++steps;
        }

        static Camera cam;
        cam.yaw = (float)simTime * 0.15f;
        cam.pitch = 0.42f;
        cam.pos = Vec3{ sinf(cam.yaw), 0.f, cosf(cam.yaw) } * -170.f + Vec3{0, 70.f, 0};

        float vp[16];
        cam.viewProj(vp, aspect);

        ID3D11DeviceContext* ctx = gfx.ctx();
        ctx->UpdateSubresource(cbFrame, 0, nullptr, vp, 0, 0);

        gfx.beginFrame(0.45f, 0.65f, 0.90f);
        ctx->IASetInputLayout(il);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->PSSetShader(ps, nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, &cbFrame);
        ctx->VSSetConstantBuffers(1, 1, &cbObj);

        ctx->UpdateSubresource(cbObj, 0, nullptr, identity, 0, 0);
        mountain.draw(ctx);

        for (auto& b : phys.boxes_) {
            float c = cosf(b.yaw), s = sinf(b.yaw);
            float w[16] = {
                 c, 0, s, 0,
                 0, 1, 0, 0,
                -s, 0, c, 0,
                 b.pos.x, b.pos.y, b.pos.z, 1
            };
            // scale axes
            for (int k = 0; k < 16; ++k) w[k] *= (k % 4 == 3) ? 1.f : 1.f;
            w[0] *= b.hx*2; w[1] *= b.hx*2; w[2] *= b.hx*2;
            w[4] *= b.hy*2; w[5] *= b.hy*2; w[6] *= b.hy*2;
            w[8] *= b.hz*2; w[9] *= b.hz*2; w[10] *= b.hz*2;

            ctx->UpdateSubresource(cbObj, 0, nullptr, w, 0, 0);
            crateMesh.draw(ctx);
        }
        gfx.endFrame();

        memcpy(prevKeys, input.keys, sizeof(prevKeys));
        input.endFrame();
        ++frame;
    }

    crateMesh.shutdown(); mountain.shutdown(); gfx.shutdown();
    printf("clean exit after %llu frames\n", (unsigned long long)frame);
    return 0;
}
