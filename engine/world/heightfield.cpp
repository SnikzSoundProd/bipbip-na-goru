#include "world/heightfield.h"
#include "core/math.h"
#include <cmath>
#include <algorithm>
#include <array>

namespace bip {

// --- value noise -----------------------------------------------------------
// Hash-based gradient-free noise; deterministic across runs (no global state).
static float hash2(int x, int z, uint64_t seed) {
    uint64_t h = (uint64_t)(uint32_t)x * 0x9E3779B97F4A7C15ull
               ^ (uint64_t)(uint32_t)z * 0xC2B2AE3D27D4EB4Full
               ^ seed;
    h ^= h >> 33; h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33; h *= 0xC4CEB9FE1A85EC53ull;
    h ^= h >> 33;
    return (float)(h >> 40) * (1.f / 16777216.f); // [0,1)
}

static float smoothstep(float t) { return t * t * (3.f - 2.f * t); }

static float valueNoise(float x, float z, uint64_t seed) {
    int xi = (int)floorf(x), zi = (int)floorf(z);
    float tx = x - (float)xi, tz = z - (float)zi;
    float v00 = hash2(xi,     zi,     seed);
    float v10 = hash2(xi + 1, zi,     seed);
    float v01 = hash2(xi,     zi + 1, seed);
    float v11 = hash2(xi + 1, zi + 1, seed);
    float sx = smoothstep(tx), sz = smoothstep(tz);
    float a = v00 + (v10 - v00) * sx;
    float b = v01 + (v11 - v01) * sx;
    return a + (b - a) * sz; // [0,1]
}

void HeightField::generate(uint64_t seed, float worldSize, int n) {
    worldSize_ = worldSize; n_ = n;

    auto heightFn = [&](float wx, float wz) -> float {
        // domain: [-worldSize/2 .. +worldSize/2]
        float u = wx / worldSize * 8.f;   // base frequency over the map
        float v = wz / worldSize * 8.f;
        float amp = 1.f, freq = 1.f, sum = 0.f, norm = 0.f;
        for (int o = 0; o < 5; ++o) {
            float n01 = valueNoise(u * freq + 13.7f * o, v * freq + 7.3f * o, seed);
            float ridge = 1.f - fabsf(n01 * 2.f - 1.f);      // [0..1], crest at 1
            sum += amp * ridge * ridge;
            norm += amp;
            amp *= 0.5f; freq *= 2.03f;                       // non-integer lacunarity
        }
        float h = sum / norm;                                 // [0..1]
        // central peak: radial falloff from center
        float r = sqrtf(wx*wx + wz*wz) / (worldSize * 0.5f);  // [0..~1.4]
        float peak = (1.f - r*r);                             // high center
        h = h * 0.35f + peak * 0.65f;
        return h * 60.f;                                      // up to ~60m tall
    };

    verts_.clear(); idx_.clear();
    verts_.reserve((size_t)n * n);
    idx_.reserve((size_t)(n-1) * (n-1) * 6);

    const float step = worldSize / (n - 1);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            float wx = -worldSize * 0.5f + i * step;
            float wz = -worldSize * 0.5f + j * step;
            float h = heightFn(wx, wz);

            Vertex vt{};
            vt.pos[0] = wx; vt.pos[1] = h; vt.pos[2] = wz;

            // normal via central differences of heightFn
            float e = step;
            float hx0 = heightFn(wx - e, wz), hx1 = heightFn(wx + e, wz);
            float hz0 = heightFn(wx, wz - e), hz1 = heightFn(wx, wz + e);
            Vec3 nv{ -(hx1-hx0)/(2*e), 1.f, -(hz1-hz0)/(2*e) };
            nv = normalize(nv);
            vt.normal[0] = nv.x; vt.normal[1] = nv.y; vt.normal[2] = nv.z;

            vt.uv[0] = vt.uv[1] = 0.f;

            // biome tint by height+slope
            float slope = 1.f - nv.y;                         // 0 flat .. 1 cliff
            Vec3 col;
            if (h > 42.f)          col = {0.92f, 0.94f, 0.97f}; // snow
            else if (h > 26.f)     col = {0.45f, 0.44f, 0.47f}; // rock
            else                   col = {0.36f, 0.55f, 0.30f}; // grass
            // blend rock onto steep slopes everywhere
            float rocky = std::min(1.f, slope * 3.5f);
            col = col * (1.f - rocky) + Vec3{0.42f, 0.40f, 0.43f} * rocky;
            vt.color[0] = col.x; vt.color[1] = col.y; vt.color[2] = col.z;

            verts_.push_back(vt);
        }
    }
    for (int j = 0; j < n - 1; ++j) {
        for (int i = 0; i < n - 1; ++i) {
            uint32_t a = j * n + i, b = a + 1, c = a + n, d = c + 1;
            idx_.insert(idx_.end(), {a, c, b, b, c, d});
        }
    }
}

float HeightField::heightAt(float x, float z) const {
    if (!n_) return 0.f;
    const float step = worldSize_ / (n_ - 1);
    float fi = (x + worldSize_ * 0.5f) / step;
    float fj = (z + worldSize_ * 0.5f) / step;
    fi = std::max(0.f, std::min((float)(n_-1), fi));
    fj = std::max(0.f, std::min((float)(n_-1), fj));
    int i0 = (int)fi, j0 = (int)fj;
    int i1 = std::min(i0+1, n_-1), j1 = std::min(j0+1, n_-1);
    float tx = fi - i0, tz = fj - j0;
    float h00 = verts_[j0*n_+i0].pos[1], h10 = verts_[j0*n_+i1].pos[1];
    float h01 = verts_[j1*n_+i0].pos[1], h11 = verts_[j1*n_+i1].pos[1];
    float a = h00 + (h10-h00)*tx;
    float b = h01 + (h11-h01)*tx;
    return a + (b-a)*tz;
}

} // namespace bip
