#pragma once
// bipbip render: camera (FPS-style debug + orbit later)
#include "core/math.h"

namespace bip {

class Camera {
public:
    Vec3 pos{0, 5, -10};
    float yaw = 0;     // radians, around Y
    float pitch = 0.3f;

    // row-major 4x4 written transposed (HLSL cbuffer-ready)
    void viewProj(float* out16, float aspect, float fovY = 1.05f, float zn = 0.1f, float zf = 2000.f) const;

    // Extract the 6 frustum planes from the row-major viewProj produced by
    // viewProj(). Each plane is (a,b,c,d); a*x+b*y+c*z+d >= 0 means INSIDE.
    // Exact derivation — no heuristics, no sign flipping:
    //   shader matrix proj = transpose(vp), so projRow_i[k] = vp[k*4+i].
    //   clip bounds: -w<=x<=w, -w<=y<=w, 0<=z<=w  =>
    //   left=r3+r0, right=r3-r0, bottom=r3+r1, top=r3-r1, near=r2, far=r3-r2.
    static void frustumPlanes(const float* vpRowMajor, float planesOut[6][4]);

private:
    static void mul16(float* o, const float* a, const float* b);
};

} // namespace bip
