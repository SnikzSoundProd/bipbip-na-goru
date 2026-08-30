// Physics sleep test (offline, no D3D).
// Drops a box onto flat terrain, steps the world, and verifies it actually
// falls asleep and stops moving — i.e. the sleep optimization is real.
#include "physics/verlet.h"
#include "world/heightfield.h"
#include <cstdio>
#include <cmath>

static int failures = 0;
#define CHECK(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); ++failures; } } while(0)

int main() {
    bip::HeightField hf;
    hf.generate(1337, 220.f, 256); // deterministic shared mountain

    bip::VerletWorld w;
    w.init(&hf);

    // find the flattest spot on the terrain (lowest height gradient), so the
    // box can actually come to rest instead of rolling down a slope
    float sx = 0.f, sz = 0.f, bestGrad = 1e9f;
    const float e = 0.35f;
    for (float z = -100.f; z <= 100.f; z += 2.f) {
        for (float x = -100.f; x <= 100.f; x += 2.f) {
            float hx1 = hf.heightAt(x + e, z), hx0 = hf.heightAt(x - e, z);
            float hz1 = hf.heightAt(x, z + e), hz0 = hf.heightAt(x, z - e);
            float g = std::fabs(hx1 - hx0) + std::fabs(hz1 - hz0);
            if (g < bestGrad) { bestGrad = g; sx = x; sz = z; }
        }
    }
    float groundY = hf.heightAt(sx, sz);
    printf("flat spot (%.1f, %.1f) groundY=%.3f grad=%.4f\n",
           (double)sx, (double)sz, (double)groundY, (double)bestGrad);

    // drop a box well above the ground
    int bi = w.addBox(bip::Vec3{sx, groundY + 6.f, sz}, 0.5f, 0.5f, 0.5f);
    CHECK(bi >= 0, "box added");

    // step long enough for the bounce to fully damp out (exponential decay)
    const int steps = 900;
    for (int i = 0; i < steps; ++i) {
        w.step(1.f / 60.f);
        if (i % 100 == 0 || i == steps - 1) {
            const bip::BoxProp& bb = w.boxes_[(size_t)bi];
            printf("  step %3d: y=%.3f vel=%.4f ang=%.4f timer=%.3f sleep=%d\n",
                   i, (double)bb.pos.y,
                   (double)std::sqrt(bb.vel.x*bb.vel.x+bb.vel.y*bb.vel.y+bb.vel.z*bb.vel.z),
                   (double)std::sqrt(bb.angVel.x*bb.angVel.x+bb.angVel.y*bb.angVel.y+bb.angVel.z*bb.angVel.z),
                   (double)bb.sleepTimer, (int)bb.sleeping);
        }
    }

    const bip::BoxProp& b = w.boxes_[(size_t)bi];
    printf("after %d steps: pos=(%.3f, %.3f, %.3f) vel=%.4f angvel=%.4f sleeping=%d\n",
           steps, b.pos.x, b.pos.y, b.pos.z,
           (double)std::sqrt(b.vel.x*b.vel.x + b.vel.y*b.vel.y + b.vel.z*b.vel.z),
           (double)std::sqrt(b.angVel.x*b.angVel.x + b.angVel.y*b.angVel.y + b.angVel.z*b.angVel.z),
           (int)b.sleeping);

    CHECK(b.sleeping, "box falls asleep after settling");
    // once asleep, velocity must be zeroed
    CHECK(std::sqrt(b.vel.x*b.vel.x + b.vel.y*b.vel.y + b.vel.z*b.vel.z) < 1e-4f,
          "sleeping box has zero velocity");
    // and it must be resting on/near the ground, not sunk or floating
    CHECK(std::fabs(b.pos.y - groundY) < 2.5f, "sleeping box rests near terrain height");

    // position must be stable: stepping further must not move it
    bip::Vec3 before = b.pos;
    for (int i = 0; i < 60; ++i) w.step(1.f / 60.f);
    bip::Vec3 after = w.boxes_[(size_t)bi].pos;
    float moved = std::sqrt((after.x-before.x)*(after.x-before.x)
                          + (after.y-before.y)*(after.y-before.y)
                          + (after.z-before.z)*(after.z-before.z));
    printf("drift over next 60 steps: %.6f\n", (double)moved);
    CHECK(moved < 1e-4f, "sleeping box does not drift");

    // sleepingBoxCount must reflect reality
    CHECK(w.sleepingBoxCount() == 1, "sleepingBoxCount reports 1");

    if (failures == 0) { printf("PHYSICS_SLEEP_TEST_PASS\n"); return 0; }
    printf("PHYSICS_SLEEP_TEST_FAIL (%d)\n", failures);
    return 1;
}
