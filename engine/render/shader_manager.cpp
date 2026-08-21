#include "render/shader_manager.h"
#include <d3dcompiler.h>
#include <cstdio>

namespace bip {

bool ShaderManager::init(ID3D11Device* device) {
    device_ = device;
    return true;
}

void ShaderManager::shutdown() { device_ = nullptr; }

bool ShaderManager::compileFile(const std::string& path, const std::string& entry,
                                const char* target, ID3DBlob** outBlob,
                                std::string* outErrors) {
    // D3DCompileFromFile takes a wide path; our asset tree is ASCII so this is lossless.
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompileFromFile(wpath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    entry.c_str(), target, flags, 0, outBlob, &err);
    if (FAILED(hr)) {
        if (outErrors) {
            outErrors->clear();
            if (err) outErrors->append((char*)err->GetBufferPointer(), err->GetBufferSize());
            else outErrors->append("D3DCompileFromFile failed (file missing?)");
        }
        if (err) err->Release();
        return false;
    }
    if (err) err->Release();
    return true;
}

bool ShaderManager::loadVertexShader(const std::string& path, const std::string& entry,
                                     const D3D11_INPUT_ELEMENT_DESC* layout, UINT layoutCount,
                                     ID3D11VertexShader** outVs, ID3D11InputLayout** outIl,
                                     std::string* outErrors) {
    ID3DBlob* vs = nullptr;
    if (!compileFile(path, entry, "vs_5_0", &vs, outErrors)) return false;
    HRESULT hr = device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(),
                                             nullptr, outVs);
    if (FAILED(hr)) { vs->Release(); return false; }
    hr = device_->CreateInputLayout(layout, layoutCount, vs->GetBufferPointer(),
                                    vs->GetBufferSize(), outIl);
    vs->Release();
    return SUCCEEDED(hr);
}

bool ShaderManager::loadPixelShader(const std::string& path, const std::string& entry,
                                    ID3D11PixelShader** outPs, std::string* outErrors) {
    ID3DBlob* ps = nullptr;
    if (!compileFile(path, entry, "ps_5_0", &ps, outErrors)) return false;
    HRESULT hr = device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(),
                                            nullptr, outPs);
    ps->Release();
    return SUCCEEDED(hr);
}

} // namespace bip
