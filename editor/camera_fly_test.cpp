// Camera fly-to test (offline, no D3D).
// Verifies the smooth camera transition used by the editor: easing,
// start/end positions, and that it actually finishes.
#include "camera_fly.h"
#include <cstdio>
#include <cmath>

static int failures = 0;
#define CHECK(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); ++failures; } } while(0)

static float dist(const bip::Vec3& a, const bip::Vec3& b) {
    float dx=a.x-b.x, dy=a.y-b.y, dz=a.z-b.z;
    return std::sqrt(dx*dx+dy*dy+dz*dz);
}

int main() {
    using namespace bip;

    // easing sanity
    CHECK(std::fabs(CameraFly::easeInOut(0.f) - 0.f) < 1e-5f, "ease(0)=0");
    CHECK(std::fabs(CameraFly::easeInOut(1.f) - 1.f) < 1e-5f, "ease(1)=1");
    CHECK(std::fabs(CameraFly::easeInOut(0.5f) - 0.5f) < 1e-5f, "ease(0.5)=0.5");
    CHECK(CameraFly::easeInOut(0.25f) < 0.25f, "ease is slow at the start");
    CHECK(CameraFly::easeInOut(0.75f) > 0.75f, "ease is fast near the end");

    // a flight from A to B
    CameraFly fly;
    Vec3 from{0, 0, 0}, to{100, 0, 0};
    fly.start(from, to, 0.f, 1.f, 0.f, 0.5f, 0.85f);
    CHECK(fly.flying(), "flight starts");

    // first update: must be near the START (not teleport to the end)
    Vec3 p; float yaw, pitch;
    fly.update(1.f/60.f, &p, &yaw, &pitch);
    CHECK(dist(p, from) < 15.f, "first frame stays near the start (smooth)");
    CHECK(dist(p, to)   > 80.f, "first frame is far from the destination");

    // run the whole flight
    float guard = 0.f;
    while (fly.flying() && guard < 5.f) { fly.update(1.f/60.f, &p, &yaw, &pitch); guard += 1.f/60.f; }
    CHECK(!fly.flying(), "flight finishes");
    CHECK(guard < 1.5f, "flight takes about the requested duration");
    CHECK(dist(p, to) < 0.01f, "flight lands exactly on the target");
    CHECK(std::fabs(yaw - 1.f) < 1e-4f, "yaw reaches target");
    CHECK(std::fabs(pitch - 0.5f) < 1e-4f, "pitch reaches target");

    // a moving target (player keeps running during the intro flight)
    CameraFly fly2;
    fly2.start(Vec3{0,0,0}, Vec3{10,0,0}, 0.f, 0.f, 0.f, 0.f, 0.5f);
    float g2 = 0.f;
    Vec3 moving{10,0,0};
    while (fly2.flying() && g2 < 3.f) {
        moving.x += 4.f * (1.f/60.f);        // target moves right
        fly2.retarget(moving);
        fly2.update(1.f/60.f, &p, &yaw, &pitch);
        g2 += 1.f/60.f;
    }
    CHECK(!fly2.flying(), "moving-target flight finishes");
    CHECK(dist(p, moving) < 1.0f, "lands on the moved target");

    if (failures == 0) { printf("CAMERA_FLY_TEST_PASS\n"); return 0; }
    printf("CAMERA_FLY_TEST_FAIL (%d)\n", failures);
    return 1;
}
