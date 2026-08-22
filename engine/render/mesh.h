#pragma once
// bipbip render: vertex/index buffers, draw call
#include <d3d11.h>
#include <cstdint>
#include <vector>

namespace bip {

// matches basic_vs.hlsl VSIn
struct Vertex {
    float pos[3];
    float normal[3];
    float uv[2];
    float color[3];
};

class Mesh {
public:
    bool init(ID3D11Device* device, const Vertex* verts, uint32_t vcount,
              const uint32_t* indices, uint32_t icount);
    void shutdown();
    void draw(ID3D11DeviceContext* ctx) const;

    uint32_t indexCount() const { return icount_; }

private:
    ID3D11Buffer* vb_ = nullptr;
    ID3D11Buffer* ib_ = nullptr;
    uint32_t icount_ = 0;
};

// helpers to build geometry
namespace geom {
std::vector<Vertex> box(float sx, float sy, float sz);          // centered cube, white
std::vector<Vertex> boxColored(float sx, float sy, float sz,
                                float r, float g, float b);     // tinted cube
std::vector<uint32_t> boxIndices();                              // 36 indices
}

} // namespace bip
