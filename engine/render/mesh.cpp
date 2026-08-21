#include "render/mesh.h"
#include "core/math.h"
#include <cmath>

namespace bip {

bool Mesh::init(ID3D11Device* device, const Vertex* verts, uint32_t vcount,
                const uint32_t* indices, uint32_t icount) {
    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.ByteWidth = (UINT)(sizeof(Vertex) * vcount);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd{ verts, 0, 0 };
    if (FAILED(device->CreateBuffer(&bd, &sd, &vb_))) return false;

    bd.ByteWidth = (UINT)(sizeof(uint32_t) * icount);
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    sd = { indices, 0, 0 };
    if (FAILED(device->CreateBuffer(&bd, &sd, &ib_))) return false;

    icount_ = icount;
    return true;
}

void Mesh::shutdown() {
    if (vb_) vb_->Release();
    if (ib_) ib_->Release();
    vb_ = ib_ = nullptr;
}

void Mesh::draw(ID3D11DeviceContext* ctx) const {
    UINT stride = sizeof(Vertex), offset = 0;
    ctx->IASetVertexBuffers(0, 1, &vb_, &stride, &offset);
    ctx->IASetIndexBuffer(ib_, DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->DrawIndexed(icount_, 0, 0);
}

namespace geom {

std::vector<Vertex> box(float sx, float sy, float sz) {
    sx *= 0.5f; sy *= 0.5f; sz *= 0.5f;
    // 6 faces x 4 verts; normals per face; white color default
    const float px[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    std::vector<Vertex> v; v.reserve(24);
    for (int f = 0; f < 6; ++f) {
        Vec3 n{px[f][0], px[f][1], px[f][2]};
        // build tangent basis
        Vec3 t = (fabsf(n.x) > 0.9f) ? Vec3{0,1,0} : Vec3{1,0,0};
        Vec3 b = cross(n, t);
        t = cross(b, n);
        Vec3 corners[4] = {
            n + t + b, n - t + b, n - t - b, n + t - b
        };
        for (int c = 0; c < 4; ++c) {
            Vertex vt{};
            vt.pos[0] = corners[c].x * sx; vt.pos[1] = corners[c].y * sy; vt.pos[2] = corners[c].z * sz;
            vt.normal[0] = n.x; vt.normal[1] = n.y; vt.normal[2] = n.z;
            vt.uv[0] = (c == 0 || c == 3) ? 0.f : 1.f;
            vt.uv[1] = (c < 2) ? 0.f : 1.f;
            vt.color[0] = vt.color[1] = vt.color[2] = 1.f;
            v.push_back(vt);
        }
    }
    return v;
}

std::vector<uint32_t> boxIndices() {
    std::vector<uint32_t> idx; idx.reserve(36);
    for (uint32_t f = 0; f < 6; ++f) {
        uint32_t b = f * 4;
        uint32_t quad[6] = { b, b+1, b+2, b, b+2, b+3 };
        idx.insert(idx.end(), quad, quad+6);
    }
    return idx;
}

} // namespace geom
} // namespace bip
