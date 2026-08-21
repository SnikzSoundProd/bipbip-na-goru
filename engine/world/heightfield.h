#pragma once
// bipbip world: seeded value-noise heightfield -> mesh geometry
#include "render/mesh.h"
#include <cstdint>
#include <vector>

namespace bip {

// Deterministic xorshift64* — same seed => same mountain (network will rely on this)
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint64_t next() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 0x2545F4914F6CDD1Dull;
    }
    float unit() { return (float)(next() >> 40) * (1.f / 16777216.f); } // [0,1)
    float signedUnit() { return unit() * 2.f - 1.f; }
};

class HeightField {
public:
    // worldSize: meters across; n: grid resolution (verts per side)
    void generate(uint64_t seed, float worldSize, int n);

    const std::vector<Vertex>& vertices() const { return verts_; }
    const std::vector<uint32_t>& indices() const { return idx_; }

    float heightAt(float x, float z) const;      // bilinear sample (for spawning objects)
    float worldSize() const { return worldSize_; }
    int   res() const { return n_; }

    std::vector<Vertex> verts_;
    std::vector<uint32_t> idx_;
    float worldSize_ = 0;
    int n_ = 0;
};

} // namespace bip
