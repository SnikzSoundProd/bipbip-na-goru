// bipbip — «БИП БИП НА ГОРУ»
// Task 0.5 smoke test: Win32 window + DX11 clear-color loop.
#include "platform/window_win32.h"
#include "render/dx11_device.h"
#include <cmath>
#include <cstdio>

using namespace bip;

int main() {
    Window window;
    if (!window.create("BIP BIP NA GORU — dx11 smoke", 1280, 720)) {
        fprintf(stderr, "window create failed\n");
        return 1;
    }
    Dx11Device gfx;
    if (!gfx.init(window.handle(), 1280, 720)) {
        fprintf(stderr, "d3d11 init failed\n");
        return 2;
    }

    InputState input;
    uint64_t frame = 0;
    while (!window.shouldClose()) {
        window.pumpMessages(input);
        if (input.down(VK_ESCAPE)) break;

        // animate clear color so we can SEE frames advancing
        float t = (frame % 600) / 600.f;
        float r = 0.5f + 0.5f * sinf(t * 6.2831853f);
        float b = 1.f - r;

        RECT rc;
        GetClientRect(window.handle(), &rc);
        if (rc.right > 0 && rc.bottom > 0)
            gfx.resize(rc.right, rc.bottom);

        gfx.beginFrame(r * 0.35f, 0.15f, b * 0.45f);
        // TODO: scene draw
        gfx.endFrame();
        input.endFrame();
        ++frame;
    }

    gfx.shutdown();
    printf("clean exit after %llu frames\n", (unsigned long long)frame);
    return 0;
}
