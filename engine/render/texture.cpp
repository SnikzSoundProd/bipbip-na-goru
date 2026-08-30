#include "render/texture.h"
#include <wincodec.h> // WIC
#include <cstdint>
#include <cstdio>
#include <vector>

namespace bip {

bool Texture::loadFromPath(ID3D11Device* device, const std::string& path) {
    // Free previous contents.
    shutdown();

    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory1, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IWICImagingFactory, (void**)&factory)))
        return false;

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT hr = factory->CreateDecoderFromFilename(
        std::wstring(path.begin(), path.end()).c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) { factory->Release(); return false; }

    IWICBitmapFrameDecode* frame = nullptr;
    decoder->GetFrame(0, &frame);

    IWICFormatConverter* converter = nullptr;
    factory->CreateFormatConverter(&converter);
    converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                          nullptr, 0.0f, WICBitmapPaletteTypeMedianCut);

    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    w_ = (int)w; h_ = (int)h;

    std::vector<uint8_t> pixels(w * h * 4);
    converter->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data());

    D3D11_TEXTURE2D_DESC td{};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{ pixels.data(), (UINT)(w * 4), 0 };

    ID3D11Texture2D* tex = nullptr;
    hr = device->CreateTexture2D(&td, &sd, &tex);
    if (SUCCEEDED(hr)) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
        srvd.Format = td.Format;
        srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvd.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(tex, &srvd, &srv_);
        tex->Release();
    }

    D3D11_SAMPLER_DESC ss{};
    ss.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    ss.AddressU = ss.AddressV = ss.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    ss.MaxAnisotropy = 1; ss.ComparisonFunc = D3D11_COMPARISON_NEVER;
    ss.MinLOD = 0; ss.MaxLOD = D3D11_FLOAT32_MAX;
    device->CreateSamplerState(&ss, &sampler_);

    converter->Release(); frame->Release(); decoder->Release(); factory->Release();
    return srv_ != nullptr;
}

bool Texture::init(ID3D11Device* device, const std::string& path) {
    return loadFromPath(device, path);
}

bool Texture::reload(ID3D11Device* device, const std::string& path) {
    return loadFromPath(device, path);
}

void Texture::shutdown() {
    if (srv_) { srv_->Release(); srv_ = nullptr; }
    if (sampler_) { sampler_->Release(); sampler_ = nullptr; }
}

} // namespace bip
