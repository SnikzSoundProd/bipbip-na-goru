#include "render/dx11_device.h"
#include <d3dcompiler.h>
#include <cstdio>

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
    scd.Flags = 0;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
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

    // Gemini Lake is FL 11_0 at best (no FL12 runtime on old drivers) — fine.
    if (!createBackbufferTargets()) return false;
    width_ = width; height_ = height;

    // default raster/blend/depth-stencil-less pipeline state
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

void Dx11Device::resize(int width, int height) {
    if (!swapchain_ || width <= 0 || height <= 0) return;
    if (rtv_) { rtv_->Release(); rtv_ = nullptr; }
    swapchain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    createBackbufferTargets();
    width_ = width; height_ = height;
}

void Dx11Device::beginFrame(float r, float g, float b) {
    const float clear[4] = {r, g, b, 1.f};
    ctx_->OMSetRenderTargets(1, &rtv_, nullptr);
    ctx_->ClearRenderTargetView(rtv_, clear);
    D3D11_VIEWPORT vp{0, 0, (float)width_, (float)height_, 0.f, 1.f};
    ctx_->RSSetViewports(1, &vp);
}

void Dx11Device::endFrame() {
    swapchain_->Present(1, 0);
}

void Dx11Device::shutdown() {
    if (rtv_) rtv_->Release();
    if (swapchain_) swapchain_->Release();
    if (ctx_) ctx_->Release();
    if (device_) device_->Release();
}

} // namespace bip
