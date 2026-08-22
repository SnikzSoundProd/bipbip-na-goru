#pragma once
// bipbip gameplay: run state — stamina, checkpoint respawn, timer, summit win.
#include "core/math.h"
#include <cstdint>

namespace bip {

class HeightField;

struct RunState {
    // stamina
    float stamina = 100.f;
    bool exhausted = false;      // stamina hit 0: hands slip, can't grab for a bit

    // checkpoint
    Vec3 respawn{};
    int lastCheckpoint = -1;

    // stats
    double runTime = 0.0;
    int falls = 0;
    bool finished = false;
    double finishTime = 0.0;

    void reset(const Vec3& spawn) {
        stamina = 100.f; exhausted = false;
        respawn = spawn;
        lastCheckpoint = -1;
        runTime = 0.0; falls = 0;
        finished = false; finishTime = 0.0;
    }
};

class Gameplay {
public:
    void init(const HeightField* hf) { hf_ = hf; }

    // per fixed step: player pelvis pos, is hanging, grab buttons, fell-off check
    void tick(RunState& rs, const Vec3& pelvis, bool hanging, bool grabbedThisStep,
              double dt);
    void onFall(RunState& rs, Vec3* outRespawn);

private:
    const HeightField* hf_ = nullptr;
};

} // namespace bip
