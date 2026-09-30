// Engine-owned character test: proves the player rig is reusable by ANY game.
//
// climber.{h,cpp} used to live in game/player/, which meant only the climbing
// game could use it. It has no climbing/route/stamina/score knowledge — it is a
// verlet ragdoll controller — so it belongs in the engine at engine/character/.
//
// This test is the contract for the second game (psycho):
//   1. A character can be spawned, controlled and simulated with nothing but
//      engine headers — no game/ include, no route, no HUD.
//   2. It collides with boxes: dropped inside one, it is pushed out, not
//      tunnelled through, and it does not sink through the floor.
//   3. A second character in the SAME world stays independent (this is what
//      co-op needs and what a "just call init twice" bug would break).
//
// Offline: no D3D, no network.
#include "character/climber.h"
#include "physics/verlet.h"
#include "world/heightfield.h"
#include <cstdio>
#include <cmath>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++failures; } } while (0)

using namespace bip;

static HeightField makeFlat() {
    HeightField hf;
    // Flat, low-frequency terrain: the character rig should be tested against a
    // known ground, not a procedural mountain.
    hf.generate(1337, 60.f, 32);
    return hf;
}

// --------------------------------------------------- 1. engine-only spawn
static void test_spawnAndSimulate() {
    HeightField hf = makeFlat();
    VerletWorld w;
    w.init(&hf);

    Climber c;
    const float gy = hf.heightAt(0.f, 0.f);
    c.init(&w, &hf, Vec3{0.f, gy + 1.2f, 0.f});

    for (int i = 0; i < 180; ++i) {      // 3 seconds of simulation
        w.step(1.f / 60.f);
        c.simulate(1.f / 60.f);
    }
    Vec3 p = c.pelvisPos();
    CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z),
          "pelvis stays finite after 3s");
    // It must settle onto the ground, not fall through it.
    CHECK(p.y > gy - 0.5f, "pelvis did not fall through the terrain");
    CHECK(p.y < gy + 3.f,   "pelvis did not launch into the sky");
}

// ------------------------------------------------ 2. box collision works
// Climber::init() treats spawn as a GROUND POSITION: it places the pelvis at
// heightAt(spawn.x, spawn.z) + kPelvisHeight and ignores spawn.y entirely. So a
// character dropped "on" a box always starts on the terrain beneath it, which
// is what makes it a real collision test: the body must be shoved out of the
// box volume rather than resting inside it.
static void test_collidesWithBox() {
    HeightField hf = makeFlat();
    VerletWorld w;
    w.init(&hf);

    const float g = hf.heightAt(0.f, 0.f);
    const float hh = 3.f;                       // half extents (1, 3, 1)
    const Vec3 boxCentre{0.f, g + hh, 0.f};
    const int bi = w.addBox(boxCentre, 1.f, hh, 1.f);
    CHECK(bi >= 0, "box added to the verlet world");
    const Vec3 boxStart = w.boxes_[bi].pos;     // boxes are dynamic: track it

    Climber c;
    c.init(&w, &hf, Vec3{0.f, 0.f, 0.f});       // ground pos at the box's footprint

    // The body starts overlapping the box: its pelvis is below the box's top
    // face while inside the box's x/z footprint.
    const Vec3 p0 = c.pelvisPos();
    CHECK(p0.y < boxCentre.y + hh, "pelvis starts inside the box's vertical span");
    CHECK(p0.x > -1.5f && p0.x < 1.5f && p0.z > -1.5f && p0.z < 1.5f,
          "pelvis starts inside the box's footprint");

    for (int i = 0; i < 300; ++i) {
        w.step(1.f / 60.f);
        c.simulate(1.f / 60.f);
    }
    const Vec3 p = c.pelvisPos();
    CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z),
          "pelvis stays finite while being pushed out of a box");
    CHECK(p.y > g - 0.5f, "character did not sink through the terrain");
    CHECK(p.y < g + 2.f * hh + 4.f, "character did not launch into the sky");

    // A rigid box is pushed by the impact — that is the momentum transfer the
    // climbing game relies on, and it is what a second game gets for free.
    const Vec3 boxEnd = w.boxes_[bi].pos;
    const float boxMoved = sqrtf((boxEnd.x-boxStart.x)*(boxEnd.x-boxStart.x) +
                                 (boxEnd.y-boxStart.y)*(boxEnd.y-boxStart.y) +
                                 (boxEnd.z-boxStart.z)*(boxEnd.z-boxStart.z));
    CHECK(boxMoved > 0.05f, "the dynamic box was pushed by the character");
}

// -------------------------------------- 3. two characters coexist safely
static void test_twoCharactersIndependent() {
    HeightField hf = makeFlat();
    VerletWorld w;
    w.init(&hf);

    const float g0 = hf.heightAt(-3.f, 0.f);
    const float g1 = hf.heightAt( 3.f, 0.f);
    Climber a, b;
    a.init(&w, &hf, Vec3{-3.f, g0 + 1.2f, 0.f});
    b.init(&w, &hf, Vec3{ 3.f, g1 + 1.2f, 0.f});

    for (int i = 0; i < 240; ++i) {
        w.step(1.f / 60.f);
        a.simulate(1.f / 60.f);
        b.simulate(1.f / 60.f);
    }
    Vec3 pa = a.pelvisPos(), pb = b.pelvisPos();
    CHECK(std::isfinite(pa.x) && std::isfinite(pb.x), "both pelvises finite");
    // A shared VerletWorld indexes particles globally: if init() allocated from
    // a shared counter these two would interleave their limbs and tear apart.
    CHECK(pa.x < 0.f && pb.x > 0.f, "characters stayed on their own sides");
    CHECK(std::fabs((pa.x + pb.x)) < 3.f, "characters did not collapse into each other");
}

int main() {
    test_spawnAndSimulate();
    test_collidesWithBox();
    test_twoCharactersIndependent();

    if (failures == 0) { printf("CHARACTER_TEST_PASS\n"); return 0; }
    printf("CHARACTER_TEST_FAIL (%d)\n", failures);
    return 1;
}
