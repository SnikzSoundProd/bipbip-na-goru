#include "render/camera.h"

namespace bip {

void Camera::mul16(float* o, const float* a, const float* b) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[r*4+k] * b[k*4+c];
            o[r*4+c] = s;
        }
}

// Row-major math; final matrix transposed into HLSL-friendly column-major buffer.
void Camera::viewProj(float* out16, float aspect, float fovY, float zn, float zf) const {
    // view basis
    Vec3 fwd{ sinf(yaw)*cosf(pitch), -sinf(pitch), cosf(yaw)*cosf(pitch) };
    Vec3 worldUp{0,1,0};
    Vec3 right = normalize(cross(worldUp, fwd));
    Vec3 up = cross(fwd, right);

    float vw[16] = {
        right.x, up.x, fwd.x, 0,
        right.y, up.y, fwd.y, 0,
        right.z, up.z, fwd.z, 0,
        -dot(right,pos), -dot(up,pos), -dot(fwd,pos), 1
    };

    float yScale = 1.f / tanf(fovY * 0.5f);
    float xScale = yScale / aspect;
    float p[16] = {
        xScale, 0, 0, 0,
        0, yScale, 0, 0,
        0, 0, zf/(zf-zn), 1,
        0, 0, -zn*zf/(zf-zn), 0
    };

    float vp[16];
    mul16(vp, vw, p);
    // NOTE: uploaded AS-IS (row-major). HLSL mul(M, v) with column_major cbuffer
    // interprets it as the transpose, which yields exactly the row-vector
    // convention used here. Do NOT transpose again.
    for (int i = 0; i < 16; ++i) out16[i] = vp[i];
}

} // namespace bip
