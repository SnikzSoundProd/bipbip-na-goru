#include "render/dx11_device.h"
#include <cstring>

namespace bip {

bool Dx11Device::init(HWND hwnd, int width, int height) {
    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferDesc.Width = width;
    scd.BufferDesc.Height = height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 0;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.OutputWindow = hwnd;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL fls[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        fls, ARRAYSIZE(fls), D3D11_SDK_VERSION,
        &scd, &swapchain_, &device_, &got, &ctx_);
    if (FAILED(hr)) return false;

    if (!createBackbufferTargets()) return false;
    if (!createDepthTargets()) return false;
    width_ = width; height_ = height;
    return true;
}

bool Dx11Device::createBackbufferTargets() {
    ID3D11Texture2D* back = nullptr;
    if (FAILED(swapchain_->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back)))
        return false;
    HRESULT hr = device_->CreateRenderTargetView(back, nullptr, &rtv_);
    back->Release();
    return SUCCEEDED(hr);
}

bool Dx11Device::createDepthTargets() {
    // recreate depth texture + view at current size
    if (dsv_) { dsv_->Release(); dsv_ = nullptr; }
    if (dsTex_) { dsTex_->Release(); dsTex_ = nullptr; }

    D3D11_TEXTURE2D_DESC dd{};
    dd.Width = width_ > 0 ? width_ : 1280;
    dd.Height = height_ > 0 ? height_ : 720;
    dd.MipLevels = 1;
    dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count = 1;
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(device_->CreateTexture2D(&dd, nullptr, &dsTex_))) return false;
    if (FAILED(device_->CreateDepthStencilView(dsTex_, nullptr, &dsv_))) return false;

    if (!dss_) {
        // standard less-equal depth test with writing
        D3D11_DEPTH_STENCIL_DESC ds{};
        ds.DepthEnable = TRUE;
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        if (FAILED(device_->CreateDepthStencilState(&ds, &dss_))) return false;

        // UI overlay: no depth at all
        D3D11_DEPTH_STENCIL_DESC nd{};
        nd.DepthEnable = FALSE;
        nd.StencilEnable = FALSE;
        if (FAILED(device_->CreateDepthStencilState(&nd, &dssNoDepth_))) return false;
    }
    return true;
}

void Dx11Device::resize(int width, int height) {
    if (!swapchain_ || width <= 0 || height <= 0) return;
    if (width == width_ && height == height_) return;   // no-op guard
    if (rtv_) { rtv_->Release(); rtv_ = nullptr; }
    swapchain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    createBackbufferTargets();
    width_ = width; height_ = height;
    createDepthTargets();                                // depth follows the size
}

void Dx11Device::beginFrame(float r, float g, float b) {
    const float clear[4] = {r, g, b, 1.f};
    ctx_->ClearDepthStencilView(dsv_, D3D11_CLEAR_DEPTH, 1.f, 0);
    ctx_->OMSetRenderTargets(1, &rtv_, dsv_);            // depth bound!
    ctx_->OMSetDepthStencilState(dss_, 0);
    ctx_->ClearRenderTargetView(rtv_, clear);
    D3D11_VIEWPORT vp{0, 0, (float)width_, (float)height_, 0.f, 1.f};
    ctx_->RSSetViewports(1, &vp);
}

void Dx11Device::beginUI() {
    // overlays draw after all 3D with depth OFF so they always show on top
    if (dssNoDepth_) ctx_->OMSetDepthStencilState(dssNoDepth_, 0);
}

void Dx11Device::endFrame() {
    swapchain_->Present(1, 0);
}

void Dx11Device::shutdown() {
    if (rtv_) rtv_->Release();
    if (dsTex_) dsTex_->Release();
    if (dsv_) dsv_->Release();
    if (dss_) dss_->Release();
    if (dssNoDepth_) dssNoDepth_->Release();
    if (swapchain_) swapchain_->Release();
    if (ctx_) ctx_->Release();
    if (device_) device_->Release();
}

} // namespace bip
