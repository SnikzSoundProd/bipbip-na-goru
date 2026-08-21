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

private:
    static void mul16(float* o, const float* a, const float* b);
};

} // namespace bip
