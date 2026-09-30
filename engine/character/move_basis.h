#pragma once
// bipbip character: camera-relative movement basis.
//
// Every game on this engine must agree on what "forward" and "right" mean for
// the player, or WASD drives the avatar in different directions per game. This
// was a real bug in psycho: it hand-rolled `sy*fwd - cy*right` inline, which is
// not the right vector, so strafing steered backwards and S/A did nothing.
//
// Convention (shared with the climbing game):
//   forward = ( sin yaw, 0,  cos yaw)
//   right   = ( cos yaw, 0, -sin yaw)
// With this pair, at yaw = 0 the camera looks toward +Z and D strafes toward
// +X. `right` is deliberately the negative rotation of `forward` about Y so the
// basis is right-handed and matches the chase camera's own back vector.
#include "core/math.h"

namespace bip {

inline Vec3 forwardOf(float yaw) { return Vec3{ sinf(yaw), 0.f, cosf(yaw) }; }
inline Vec3 rightOf(float yaw)   { return Vec3{ cosf(yaw), 0.f, -sinf(yaw) }; }

// Build a unit-length world-space move vector from four key states.
// Each input is 1.0 when the key is held. Opposite keys cancel exactly, and a
// diagonal stays unit length instead of being faster than a cardinal.
inline Vec3 moveBasisFor(float yaw, float fwd, float back, float left, float right) {
    const Vec3 f = forwardOf(yaw);
    const Vec3 r = rightOf(yaw);
    Vec3 m{};
    if (fwd)   m = m + f;
    if (back)  m = m - f;
    if (left)  m = m - r;
    if (right) m = m + r;
    if (m.x != 0.f || m.z != 0.f) {
        const float l = sqrtf(m.x * m.x + m.z * m.z);
        m.x /= l; m.z /= l;
    }
    return m;
}

} // namespace bip
