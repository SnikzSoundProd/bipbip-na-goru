#pragma once
// bipbip asset pipeline (scaffold): PNG/texture loader via WIC.
// Loads an image file into an ID3D11ShaderResourceView plus a linear sampler.
// Infrastructure only — not wired into the current box/capsule rendering yet.
#include <d3d11.h>
#include <string>

namespace bip {

class Texture {
public:
    Texture() = default;
    ~Texture() { shutdown(); }

    bool init(ID3D11Device* device, const std::string& path);
    void shutdown();

    ID3D11ShaderResourceView* srv() const { return srv_; }
    ID3D11SamplerState*       sampler() const { return sampler_; }
    int width() const { return w_; }
    int height() const { return h_; }

    // Manual reload (used by hot-reload). Releases old views, loads new file.
    bool reload(ID3D11Device* device, const std::string& path);

private:
    bool loadFromPath(ID3D11Device* device, const std::string& path);

    ID3D11ShaderResourceView* srv_ = nullptr;
    ID3D11SamplerState*       sampler_ = nullptr;
    int w_ = 0, h_ = 0;
};

} // namespace bip
