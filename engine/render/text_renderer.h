#pragma once
// bipbip render: tiny bitmap text renderer (5x7 font, no external deps)
#include <d3d11.h>
#include <cstdint>
#include <string>

namespace bip {

class TextRenderer {
public:
    bool init(ID3D11Device* device, int screenWidth, int screenHeight);
    void shutdown();
    void resize(int w, int h);

    // draw text in screen pixels, scale = integer multiplier of 5x7 cell
    void draw(const std::string& text, float x, float y, float scale,
              float r, float g, float b);

private:
    void updateProj();

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* ctx_ = nullptr;
    ID3D11Buffer* vb_ = nullptr;
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11InputLayout* il_ = nullptr;
    ID3D11Buffer* cb_ = nullptr;
    int sw_ = 0, sh_ = 0;
};

} // namespace bip
