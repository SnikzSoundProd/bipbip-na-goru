#include "game/world/route.h"
#include "world/heightfield.h"
#include <cmath>

namespace bip {

// Deterministic route: walk a spiral around the peak, placing holds every ~2m
// of arc with height following the terrain + climbable margin.
void Route::generate(const HeightField& hf, uint64_t seed) {
    Rng rng(seed ^ 0xACE);
    holds_.clear();

    const float ws = hf.worldSize();
    start_ = Vec3{0.f, hf.heightAt(0.f, ws*0.5f - 12.f), ws*0.5f - 12.f};
    summit_ = Vec3{0.f, hf.heightAt(0.f, 0.f) + 1.5f, 0.f};

    float baseH = hf.heightAt(start_.x, start_.z);

    // spiral from radius r0 down to 3m around center
    const int turns = 5;
    const int perTurn = 26;
    for (int t = 0; t < turns * perTurn; ++t) {
        float u = (float)t / (turns * perTurn);          // 0..1
        float ang = u * turns * 6.2831853f;
        // slight wobble so it's not a perfect spiral
        ang += sinf(u * 40.f) * 0.06f + (rng.unit()-0.5f)*0.05f;
        float r = (ws * 0.5f - 14.f) * (1.f - u) + 3.5f;
        float x = cosf(ang) * r;
        float z = sinf(ang) * r;

        // hold sits ON the terrain surface, lifted a bit (grab point on rocks)
        float h = hf.heightAt(x, z);
        // every 13th hold is a checkpoint bonfire
        bool cp = (t % 13 == 6) && u > 0.08f && u < 0.97f;
        Hold hold;
        hold.pos = Vec3{x, h + 0.9f, z};
        hold.checkpoint = cp;
        holds_.push_back(hold);

        // occasionally add a second hold slightly offset (choice of path)
        if (cp || rng.unit() < 0.18f) {
            float x2 = x + (rng.unit()-0.5f) * 2.4f;
            float z2 = z + (rng.unit()-0.5f) * 2.4f;
            Hold extra;
            extra.pos = Vec3{x2, hf.heightAt(x2, z2) + 0.9f, z2};
            extra.checkpoint = false;
            holds_.push_back(extra);
        }
    }

    summit_.y = hf.heightAt(0.f, 0.f);
}

int Route::nearest(const Vec3& p, float maxDist) const {
    int best = -1; float bd = maxDist * maxDist;
    for (size_t i = 0; i < holds_.size(); ++i) {
        Vec3 d = holds_[i].pos - p;
        float d2 = dot(d, d);
        if (d2 < bd) { bd = d2; best = (int)i; }
    }
    return best;
}

} // namespace bip
