// bipbip — «БИП БИП НА ГОРУ»
// Phase 1 check: lit rotating cube, FPS-fly debug camera (hold RMB to look).
#include "platform/window_win32.h"
#include "render/dx11_device.h"
#include "render/shader_manager.h"
#include "render/mesh.h"
#include "render/camera.h"
#include <cmath>
#include <cstdio>
#include <string>

using namespace bip;

int main() {
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

    auto verts = geom::box(1.f, 1.f, 1.f);
    auto idx = geom::boxIndices();
    Mesh cube;
    if (!cube.init(gfx.device(), verts.data(), (uint32_t)verts.size(), idx.data(), (uint32_t)idx.size()))
        return 6;

    // constant buffers
    struct CBPerFrame { float viewProj[16]; };
    struct CBPerObject { float world[16]; };
    ID3D11Buffer* cbFrame = nullptr; ID3D11Buffer* cbObj = nullptr;
    D3D11_BUFFER_DESC cbd{}; cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.ByteWidth = sizeof(CBPerFrame); cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    gfx.device()->CreateBuffer(&cbd, nullptr, &cbFrame);
    cbd.ByteWidth = sizeof(CBPerObject);
    gfx.device()->CreateBuffer(&cbd, nullptr, &cbObj);

    InputState input;
    bool prevKeys[256] = {};
    uint64_t frame = 0;
    while (!window.shouldClose()) {
        window.pumpMessages(input);
        if (input.down(VK_ESCAPE)) break;

        // resize handling
        RECT rc; GetClientRect(window.handle(), &rc);
        if (rc.right > 0 && rc.bottom > 0) gfx.resize(rc.right, rc.bottom);
        float aspect = (float)rc.right / (float)rc.bottom;

        // camera control: RMB look, WASD move, QE up/down
        static Camera cam{{0, 2.5f, -3.f}, /*yaw*/0.f, /*pitch*/0.15f};
        if (input.mouseButtons[1]) {
            cam.yaw   += input.mouseDX * 0.003f;
            cam.pitch += input.mouseDY * 0.003f;
            if (cam.pitch > 1.55f) cam.pitch = 1.55f;
            if (cam.pitch < -1.55f) cam.pitch = -1.55f;
        }
        Vec3 fwd{ sinf(cam.yaw)*cosf(cam.pitch), -sinf(cam.pitch), cosf(cam.yaw)*cosf(cam.pitch) };
        Vec3 right{ cosf(cam.yaw), 0, -sinf(cam.yaw) };
        float spd = 8.f * (1.f/60.f);
        if (input.down('W')) cam.pos = cam.pos + fwd * spd;
        if (input.down('S')) cam.pos = cam.pos - fwd * spd;
        if (input.down('A')) cam.pos = cam.pos - right * spd;
        if (input.down('D')) cam.pos = cam.pos + right * spd;
        if (input.down('E')) cam.pos = cam.pos + Vec3{0,spd,0};
        if (input.down('Q')) cam.pos = cam.pos - Vec3{0,spd,0};

        // matrices
        float vp[16];
        cam.viewProj(vp, aspect);

        float t = frame / 60.f;
        float ang = t * 0.8f;
        // world = rotY(ang) * translate(0, 1.5, 0)
        float c = cosf(ang), s = sinf(ang);
        float world[16] = {
             c, 0, s, 0,
             0, 1, 0, 0,
            -s, 0, c, 0,
             0, 1.5f, 4.0f, 1
        };

        ID3D11DeviceContext* ctx = gfx.ctx();

        ctx->UpdateSubresource(cbFrame, 0, nullptr, vp, 0, 0);
        ctx->UpdateSubresource(cbObj, 0, nullptr, world, 0, 0);

        gfx.beginFrame(0.28f, 0.42f, 0.62f); // sky-ish blue
        ctx->IASetInputLayout(il);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->PSSetShader(ps, nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, &cbFrame);
        ctx->VSSetConstantBuffers(1, 1, &cbObj);
        cube.draw(ctx);
        gfx.endFrame();

        memcpy(prevKeys, input.keys, sizeof(prevKeys));
        input.endFrame();
        ++frame;
    }

    cube.shutdown();
    gfx.shutdown();
    printf("clean exit after %llu frames\n", (unsigned long long)frame);
    return 0;
}
