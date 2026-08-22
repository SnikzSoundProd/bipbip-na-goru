#include "game/gameplay/run.h"
#include "world/heightfield.h"
#include <algorithm>

namespace bip {

void Gameplay::tick(RunState& rs, const Vec3& pelvis, bool hanging,
                    bool grabbedThisStep, double dt) {
    if (rs.finished) return;
    rs.runTime += dt;

    // stamina: hanging drains, ground regens
    if (hanging)
        rs.stamina -= 7.0f * (float)dt;
    else
        rs.stamina = std::min(100.f, rs.stamina + 18.f * (float)dt);

    // grab costs a chunk; exhausted hands slip
    if (grabbedThisStep) rs.stamina -= 2.f;

    if (rs.stamina <= 0.f) {
        rs.stamina = 0.f;
        rs.exhausted = true;
    } else if (rs.exhausted && rs.stamina > 25.f) {
        rs.exhausted = false;   // recovered enough to try again
    }

    // summit check: pelvis near peak height within radius
    float d2 = pelvis.x*pelvis.x + pelvis.z*pelvis.z;
    float topY = hf_->heightAt(0.f, 0.f);
    if (d2 < 16.f && pelvis.y > topY + 0.5f) {
        rs.finished = true;
        rs.finishTime = rs.runTime;
    }
}

void Gameplay::onFall(RunState& rs, Vec3* outRespawn) {
    ++rs.falls;
    rs.exhausted = false;
    rs.stamina = std::max(rs.stamina, 40.f);   // mercy stamina after a fall
    *outRespawn = rs.respawn;
}

} // namespace bip
