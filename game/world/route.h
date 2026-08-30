#pragma once
// bipbip gameplay: guaranteed climb route — spiral of holds from base to summit.
#include "core/math.h"
#include <cstdint>
#include <vector>

namespace bip {

class HeightField;

struct Hold {
    Vec3 pos;
    bool checkpoint;   // big orange one: sets respawn
};

class Route {
public:
    void generate(const HeightField& hf, uint64_t seed);

    // Build the hold list from an authored scene instead of generating it.
    // Lets the editor place/checkpoint holds and have the runtime honour them.
    void buildFromScene(const std::vector<Hold>& authored, const HeightField& hf);

    const std::vector<Hold>& holds() const { return holds_; }
    const Vec3& summit() const { return summit_; }
    const Vec3& start() const { return start_; }

    // nearest hold within maxDist, or -1
    int nearest(const Vec3& p, float maxDist) const;

private:
    std::vector<Hold> holds_;
    Vec3 summit_{};
    Vec3 start_{};
};

} // namespace bip
