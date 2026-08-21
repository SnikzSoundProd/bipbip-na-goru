#pragma once
// bipbip render: runtime HLSL compilation via D3DCompiler_47 + hot reload (F5 later)
#include <d3d11.h>
#include <string>
#include <vector>

namespace bip {

class ShaderManager {
public:
    bool init(ID3D11Device* device);
    void shutdown();

    // Compile from file each launch (fast enough at startup; .cso cache later).
    bool loadVertexShader(const std::string& path, const std::string& entry,
                          const D3D11_INPUT_ELEMENT_DESC* layout, UINT layoutCount,
                          ID3D11VertexShader** outVs, ID3D11InputLayout** outIl,
                          std::string* outErrors = nullptr);
    bool loadPixelShader(const std::string& path, const std::string& entry,
                         ID3D11PixelShader** outPs, std::string* outErrors = nullptr);

private:
    bool compileFile(const std::string& path, const std::string& entry,
                     const char* target, ID3DBlob** outBlob, std::string* outErrors);

    ID3D11Device* device_ = nullptr;
};

} // namespace bip
