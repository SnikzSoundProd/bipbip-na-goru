// bipbip — «БИП БИП НА ГОРУ»
// Phase 1.3 check: seeded mountain terrain, orbiting debug camera.
#include "platform/window_win32.h"
#include "render/dx11_device.h"
#include "render/shader_manager.h"
#include "render/mesh.h"
#include "render/camera.h"
#include "world/heightfield.h"
#include <cmath>
#include <cstdio>
#include <string>

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

    // --- the mountain --------------------------------------------------------
    HeightField hf;
    hf.generate(seed, /*worldSize*/300.f, /*res*/256);
    Mesh mountain;
    {
        const auto& vv = hf.vertices();
        const auto& ii = hf.indices();
        if (!mountain.init(gfx.device(), vv.data(), (uint32_t)vv.size(),
                           ii.data(), (uint32_t)ii.size())) return 6;
    }

    // constant buffers
    struct CBPerFrame { float viewProj[16]; };
    struct CBPerObject { float world[16]; };
    ID3D11Buffer* cbFrame = nullptr; ID3D11Buffer* cbObj = nullptr;
    D3D11_BUFFER_DESC cbd{}; cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.ByteWidth = sizeof(CBPerFrame); cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    gfx.device()->CreateBuffer(&cbd, nullptr, &cbFrame);
    cbd.ByteWidth = sizeof(CBPerObject);
    gfx.device()->CreateBuffer(&cbd, nullptr, &cbObj);

    // identity world matrix
    float identity[16] = { 1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1 };

    InputState input;
    bool prevKeys[256] = {};
    uint64_t frame = 0;
    while (!window.shouldClose()) {
        window.pumpMessages(input);
        if (input.down(VK_ESCAPE)) break;

        RECT rc; GetClientRect(window.handle(), &rc);
        if (rc.right > 0 && rc.bottom > 0) gfx.resize(rc.right, rc.bottom);
        float aspect = (float)rc.right / (float)rc.bottom;

        // slow auto-orbit camera around the peak
        float t = frame / 60.f;
        static Camera cam;
        cam.yaw = t * 0.15f;
        cam.pitch = 0.42f;
        cam.pos = Vec3{ sinf(cam.yaw), 0.f, cosf(cam.yaw) } * -170.f + Vec3{0, 70.f, 0};

        float vp[16];
        cam.viewProj(vp, aspect);

        ID3D11DeviceContext* ctx = gfx.ctx();
        ctx->UpdateSubresource(cbFrame, 0, nullptr, vp, 0, 0);
        ctx->UpdateSubresource(cbObj, 0, nullptr, identity, 0, 0);

        gfx.beginFrame(0.45f, 0.65f, 0.90f); // sky
        ctx->IASetInputLayout(il);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->PSSetShader(ps, nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, &cbFrame);
        ctx->VSSetConstantBuffers(1, 1, &cbObj);
        mountain.draw(ctx);
        gfx.endFrame();

        memcpy(prevKeys, input.keys, sizeof(prevKeys));
        input.endFrame();
        ++frame;
    }

    mountain.shutdown();
    gfx.shutdown();
    printf("clean exit after %llu frames\n", (unsigned long long)frame);
    return 0;
}
