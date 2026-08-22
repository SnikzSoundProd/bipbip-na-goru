#pragma once
// bipbip core math: minimal linear algebra (grow as needed)
#include <cmath>

namespace bip {

struct Vec3 {
    float x=0, y=0, z=0;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x-o.x, y-o.y, z-o.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(float s) const { return {x*s, y*s, z*s}; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline float length(const Vec3& v) { return sqrtf(dot(v, v)); }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}
inline Vec3 normalize(const Vec3& v) {
    float l = sqrtf(dot(v, v));
    return l > 1e-8f ? v * (1.f/l) : Vec3();
}

struct Quat {
    float x=0, y=0, z=0, w=1;
    Quat() = default;
    Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    Quat operator*(const Quat& o) const {
        return { w*o.x + x*o.w + y*o.z - z*o.y,
                 w*o.y - x*o.z + y*o.w + z*o.x,
                 w*o.z + x*o.y - y*o.x + z*o.w,
                 w*o.w - x*o.x - y*o.y - z*o.z };
    }
    Quat conj() const { return {-x, -y, -z, w}; }
    Quat normalized() const {
        float l = sqrtf(x*x + y*y + z*z + w*w);
        return l > 1e-10f ? Quat{x/l, y/l, z/l, w/l} : Quat{};
    }
    static Quat axisAngle(const Vec3& axis, float ang) {
        float a = ang * 0.5f, s = sinf(a);
        Vec3 n = normalize(axis);
        return {n.x*s, n.y*s, n.z*s, cosf(a)};
    }
};

// rotate vector by quaternion (q * v * conj(q), optimized)
inline Vec3 rotate(const Quat& q, const Vec3& v) {
    Vec3 u{q.x, q.y, q.z};
    Vec3 t = cross(u, v) * 2.f;
    return v + t * q.w + cross(u, t);
}

} // namespace bip
