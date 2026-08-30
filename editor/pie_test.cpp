// PIE start/stop test (offline, no D3D).
// Reproduces pressing Play/Stop in the editor: start() builds the world,
// step() runs it. This is where the editor was crashing, so it is tested.
#include "pie_world.h"
#include <cstdio>
#include <cmath>

static int failures = 0;
#define CHECK(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); ++failures; } } while(0)

int main() {
    using namespace bip;

    Scene sc;
    sc.seed = 1337;
    sc.worldSize = 220.f;
    sc.heightN = 256;
    sc.spawn[0] = 0.f; sc.spawn[1] = 0.f; sc.spawn[2] = 118.f;

    SceneBox b0; b0.pos[0]=0.f; b0.pos[1]=30.f; b0.pos[2]=0.f;
    b0.half[0]=b0.half[1]=b0.half[2]=0.5f;
    sc.boxes.push_back(b0);

    SceneHold h0; h0.pos[0]=0.f; h0.pos[1]=12.f; h0.pos[2]=100.f; h0.checkpoint=false;
    sc.holds.push_back(h0);

    PIEWorld pie;
    printf("start()...\n"); fflush(stdout);
    CHECK(pie.start(sc, nullptr), "PIE start succeeds");
    printf("start() ok, active=%d\n", (int)pie.active()); fflush(stdout);

    CHECK(pie.active(), "PIE active after start");
    CHECK(pie.phys.boxes_.size() == 1, "PIE has the authored box");
    CHECK(pie.route.holds().size() == 1, "PIE uses authored holds");

    Vec3 p0 = pie.player.pelvisPos();
    printf("player spawn = (%.2f, %.2f, %.2f)\n", (double)p0.x, (double)p0.y, (double)p0.z);

    // step with no input (as Edit mode would) — must not crash
    printf("stepping (no input)...\n"); fflush(stdout);
    for (int i = 0; i < 120; ++i) pie.step(1.f/60.f, nullptr, 0.f);
    printf("stepping ok\n"); fflush(stdout);

    // step WITH a null-ish input state (as Play mode would) — must not crash
    InputState in{};
    printf("stepping (with input)...\n"); fflush(stdout);
    for (int i = 0; i < 120; ++i) pie.step(1.f/60.f, &in, 0.f);
    printf("stepping with input ok\n"); fflush(stdout);

    // player should have fallen / settled, not NaN
    Vec3 p1 = pie.player.pelvisPos();
    printf("player after = (%.2f, %.2f, %.2f)\n", (double)p1.x, (double)p1.y, (double)p1.z);
    CHECK(std::isfinite(p1.x) && std::isfinite(p1.y) && std::isfinite(p1.z),
          "player position stays finite (no NaN)");

    printf("stop()...\n"); fflush(stdout);
    pie.stop();
    CHECK(!pie.active(), "PIE inactive after stop");
    printf("stop() ok\n"); fflush(stdout);

    // restart (pressing Play a second time) must also work
    printf("restart...\n"); fflush(stdout);
    CHECK(pie.start(sc, nullptr), "PIE restart succeeds");
    for (int i = 0; i < 60; ++i) pie.step(1.f/60.f, &in, 0.f);
    pie.stop();
    printf("restart ok\n"); fflush(stdout);

    if (failures == 0) { printf("PIE_TEST_PASS\n"); return 0; }
    printf("PIE_TEST_FAIL (%d)\n", failures);
    return 1;
}
