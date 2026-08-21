#pragma once
// bipbip render: D3D11 device + swapchain ownership
#include <d3d11.h>
#include <dxgi.h>

namespace bip {

class Dx11Device {
public:
    bool init(HWND hwnd, int width, int height);
    void shutdown();
    void resize(int width, int height);

    void beginFrame(float r, float g, float b); // bind backbuffer + clear
    void endFrame();                            // Present (vsync on)

    ID3D11Device*          device() const { return device_; }
    ID3D11DeviceContext*   ctx()    const { return ctx_; }

private:
    bool createBackbufferTargets();

    ID3D11Device*        device_    = nullptr;
    ID3D11DeviceContext* ctx_       = nullptr;
    IDXGISwapChain*      swapchain_ = nullptr;
    ID3D11RenderTargetView* rtv_    = nullptr;
    int width_ = 0, height_ = 0;
};

} // namespace bip
