#pragma once
// bipbip asset pipeline (scaffold): caches meshes + textures, supports hot-reload.
// Not yet consumed by the game's box/capsule renderer — this is the foundation
// for switching humanoid proxies to authored OBJ meshes later.
#include "render/mesh.h"
#include "render/texture.h"
#include "core/asset_watch.h"
#include <string>
#include <unordered_map>
#include <memory>

namespace bip {

class AssetManager {
public:
    void init(ID3D11Device* device, const std::string& assetRoot);

    // Load (or return cached) mesh from assets/meshes/<name>.obj.
    // Returns nullptr on first failure; reloads in place on hot-reload.
    Mesh* mesh(const std::string& name);

    // Load (or return cached) texture from assets/textures/<name>.png.
    Texture* texture(const std::string& name);

    // Call once per frame: reload any watched asset whose file changed.
    void update();

private:
    ID3D11Device* device_ = nullptr;
    std::string root_;
    AssetWatcher watcher_;

    std::unordered_map<std::string, std::unique_ptr<Mesh>> meshes_;
    std::unordered_map<std::string, std::unique_ptr<Texture>> textures_;
};

} // namespace bip
