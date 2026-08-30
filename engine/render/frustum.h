#pragma once
// bipbip render: frustum culling helper.
// Holds 6 planes (from Camera::frustumPlanes) and tests sphere/AABB visibility.
// All 6 planes are oriented so "inside" == plane . (x,y,z,1) >= 0.
#include <cmath>

namespace bip {

class Frustum {
public:
    void setPlanes(const float planes[6][4]) {
        for (int i = 0; i < 6; ++i)
            for (int k = 0; k < 4; ++k) p_[i][k] = planes[i][k];
    }

    // Sphere test: visible if the center is on the inside half-space of every
    // plane, expanded by the radius. Conservative (keeps partially-visible
    // objects), and correct because all planes share the same orientation.
    bool sphereVisible(float cx, float cy, float cz, float radius) const {
        for (int i = 0; i < 6; ++i) {
            float d = p_[i][0]*cx + p_[i][1]*cy + p_[i][2]*cz + p_[i][3];
            if (d < -radius) return false;
        }
        return true;
    }

    // AABB test: find the corner most inside each plane; if that corner is
    // still outside the plane, the whole box is outside.
    bool boxVisible(float minx, float miny, float minz,
                    float maxx, float maxy, float maxz) const {
        for (int i = 0; i < 6; ++i) {
            const float* pl = p_[i];
            float px = (pl[0] >= 0.f) ? maxx : minx;
            float py = (pl[1] >= 0.f) ? maxy : miny;
            float pz = (pl[2] >= 0.f) ? maxz : minz;
            if (pl[0]*px + pl[1]*py + pl[2]*pz + pl[3] < 0.f) return false;
        }
        return true;
    }

private:
    float p_[6][4] = {};
};

} // namespace bip
