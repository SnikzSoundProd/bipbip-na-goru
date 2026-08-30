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

// Exact frustum-plane extraction (no heuristics, no sign flipping).
// viewProj() writes a row-major matrix; HLSL reads the cbuffer column-major,
// so the shader matrix is proj = transpose(vp), i.e. projRow_i[k] = vp[k*4+i].
// Clip-space bounds for D3D: -w <= x <= w, -w <= y <= w, 0 <= z <= w.
// Substituting x = projRow_0 . v etc. gives the six planes below, each with
// "inside" == plane . (x,y,z,1) >= 0. Planes are normalized to unit normal.
void Camera::frustumPlanes(const float* m, float planesOut[6][4]) {
    // Column-major accessors into the row-major buffer: projRow_i[k] = m[k*4+i]
    auto row = [m](int i, int k) -> float { return m[k * 4 + i]; };
    auto set = [&](int i, int a, int b, float s) {
        for (int k = 0; k < 4; ++k) planesOut[i][k] = row(a, k) + s * row(b, k);
    };
    auto setNear = [&](int i) {
        for (int k = 0; k < 4; ++k) planesOut[i][k] = row(2, k);
    };
    set(0, 3, 0,  1.f); // left   = r3 + r0
    set(1, 3, 0, -1.f); // right  = r3 - r0
    set(2, 3, 1,  1.f); // bottom = r3 + r1
    set(3, 3, 1, -1.f); // top    = r3 - r1
    setNear(4);         // near   = r2
    set(5, 3, 2, -1.f); // far    = r3 - r2
    // normalize so signed distance is in world units
    for (int i = 0; i < 6; ++i) {
        float len = sqrtf(planesOut[i][0]*planesOut[i][0]
                        + planesOut[i][1]*planesOut[i][1]
                        + planesOut[i][2]*planesOut[i][2]);
        if (len > 1e-6f) for (int k = 0; k < 4; ++k) planesOut[i][k] /= len;
    }
}

} // namespace bip
