#include "render/asset_manager.h"
#include "render/obj_loader.h"
#include <cstdio>

namespace bip {

void AssetManager::init(ID3D11Device* device, const std::string& assetRoot) {
    device_ = device;
    root_ = assetRoot;
}

Mesh* AssetManager::mesh(const std::string& name) {
    auto it = meshes_.find(name);
    if (it != meshes_.end()) return it->second.get();

    std::string path = root_ + "assets/meshes/" + name + ".obj";
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;
    if (!loadObj(path, verts, indices, true)) {
        fprintf(stderr, "[assets] mesh load failed: %s\n", path.c_str());
        return nullptr;
    }
    auto m = std::make_unique<Mesh>();
    if (!m->init(device_, verts.data(), (uint32_t)verts.size(),
                 indices.data(), (uint32_t)indices.size())) {
        fprintf(stderr, "[assets] mesh buffer create failed: %s\n", path.c_str());
        return nullptr;
    }
    fprintf(stderr, "[assets] mesh loaded %s (%zu v, %zu i)\n",
            name.c_str(), verts.size(), indices.size());
    Mesh* ptr = m.get();
    meshes_[name] = std::move(m);
    watcher_.watch(path);
    return ptr;
}

Texture* AssetManager::texture(const std::string& name) {
    auto it = textures_.find(name);
    if (it != textures_.end()) return it->second.get();

    std::string path = root_ + "assets/textures/" + name + ".png";
    auto t = std::make_unique<Texture>();
    if (!t->init(device_, path)) {
        fprintf(stderr, "[assets] texture load failed: %s\n", path.c_str());
        return nullptr;
    }
    fprintf(stderr, "[assets] texture loaded %s\n", name.c_str());
    Texture* ptr = t.get();
    textures_[name] = std::move(t);
    watcher_.watch(path);
    return ptr;
}

void AssetManager::update() {
    auto changed = watcher_.poll();
    for (auto& path : changed) {
        // Re-load by matching path suffix against known caches.
        for (auto& kv : meshes_) {
            std::string p = root_ + "assets/meshes/" + kv.first + ".obj";
            if (p == path) {
                std::vector<Vertex> verts; std::vector<uint32_t> idx;
                if (loadObj(p, verts, idx, true)) {
                    kv.second->init(device_, verts.data(), (uint32_t)verts.size(),
                                    idx.data(), (uint32_t)idx.size());
                    fprintf(stderr, "[assets] hot-reloaded mesh %s\n", kv.first.c_str());
                }
            }
        }
        for (auto& kv : textures_) {
            std::string p = root_ + "assets/textures/" + kv.first + ".png";
            if (p == path) {
                kv.second->reload(device_, p);
                fprintf(stderr, "[assets] hot-reloaded texture %s\n", kv.first.c_str());
            }
        }
    }
}

} // namespace bip
