// Frustum culling test (offline, no D3D).
// Verifies Camera::frustumPlanes produces planes that correctly classify
// points in front of / behind the camera.
#include "render/camera.h"
#include "render/frustum.h"
#include <cstdio>
#include <cmath>

static int failures = 0;
#define CHECK(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); ++failures; } } while(0)

int main() {
    bip::Camera cam;
    cam.pos = bip::Vec3{0, 5, 0};
    cam.yaw = 0.f;      // looks toward +Z (fwd = {sin0*cos p, -sin p, cos0*cos p} = {0, -p, 1})
    cam.pitch = 0.f;

    float vp[16];
    cam.viewProj(vp, 16.f/9.f, 1.05f, 0.1f, 100.f);
    float planes[6][4];
    bip::Camera::frustumPlanes(vp, planes);

    bip::Frustum f;
    f.setPlanes(planes);

    // Point 10m in front of camera (along +Z from cam pos): should be visible
    CHECK(f.sphereVisible(0.f, 5.f, 10.f, 1.f), "point 10m in front visible");
    // Point 50m in front (well inside far=100): visible
    CHECK(f.sphereVisible(0.f, 5.f, 50.f, 1.f), "point 50m in front visible");
    // Point 500m in front (far beyond far plane 100): NOT visible
    CHECK(!f.sphereVisible(0.f, 5.f, 500.f, 1.f), "point far beyond far plane culled");
    // Point 5m BEHIND camera (negative Z): NOT visible
    CHECK(!f.sphereVisible(0.f, 5.f, -5.f, 1.f), "point behind camera culled");
    // Point far to the side (off to +X beyond frustum): NOT visible
    CHECK(!f.sphereVisible(80.f, 5.f, 10.f, 1.f), "point far to the right culled");
    // Close point directly at camera: visible
    CHECK(f.sphereVisible(0.f, 5.f, 1.f, 0.5f), "near point visible");

    if (failures == 0) { printf("FRUSTUM_TEST_PASS\n"); return 0; }
    printf("FRUSTUM_TEST_FAIL (%d)\n", failures);
    return 1;
}
