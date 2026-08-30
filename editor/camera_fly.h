#pragma once
// bipbip editor: smooth camera transitions ("fly to"), Unreal-style.
// Shared by the editor and its unit test so the maths is verified, not copied.
#include "core/math.h"

namespace bip {

class CameraFly {
public:
    // ease-in-out so the flight accelerates and decelerates naturally
    static float easeInOut(float t) {
        if (t <= 0.f) return 0.f;
        if (t >= 1.f) return 1.f;
        return (t < 0.5f) ? (2.f*t*t)
                          : (1.f - ((-2.f*t + 2.f)*(-2.f*t + 2.f)) * 0.5f);
    }

    void start(const Vec3& from, const Vec3& to,
               float yawFrom, float yawTo,
               float pitchFrom, float pitchTo,
               float durationSec = 0.85f) {
        from_ = from; to_ = to;
        yawFrom_ = yawFrom; yawTo_ = yawTo;
        pitchFrom_ = pitchFrom; pitchTo_ = pitchTo;
        t_ = 0.f; dur_ = (durationSec > 1e-4f) ? durationSec : 0.85f;
        flying_ = true;
    }

    // advance by dt; returns current position. Clears flying_ at the end.
    Vec3 update(float dt, Vec3* outPos, float* outYaw, float* outPitch) {
        if (flying_) {
            t_ += dt / dur_;
            if (t_ >= 1.f) { t_ = 1.f; flying_ = false; }
        }
        float k = easeInOut(t_);
        Vec3 p = from_ + (to_ - from_) * k;
        if (outPos)   *outPos   = p;
        if (outYaw)   *outYaw   = yawFrom_   + (yawTo_   - yawFrom_)   * k;
        if (outPitch) *outPitch = pitchFrom_ + (pitchTo_ - pitchFrom_) * k;
        return p;
    }

    // While flying, allow the destination to move (the player keeps running
    // during the intro flight, so the chase target must follow him).
    void retarget(const Vec3& newTo) { to_ = newTo; }

    bool flying() const { return flying_; }
    float progress() const { return t_; }

private:
    Vec3  from_{}, to_{};
    float yawFrom_ = 0.f, yawTo_ = 0.f;
    float pitchFrom_ = 0.f, pitchTo_ = 0.f;
    float t_ = 0.f, dur_ = 0.85f;
    bool  flying_ = false;
};

} // namespace bip
