#pragma once
// bipbip player: verlet ragdoll climber.
// Pelvis is semi-kinematic (crisp control), limbs are pure ragdoll on constraints.
#include "core/math.h"
#include "physics/verlet.h"
#include <cstdint>

namespace bip {

class HeightField;

struct ClimberVisual {
    // body-part half extents + colors for the renderer
    float torso[3]  = { 0.16f, 0.22f, 0.10f };
    float head[3]   = { 0.11f, 0.11f, 0.11f };
    float limbR     = 0.055f;
    Vec3 skin{ 0.96f, 0.78f, 0.62f };
    Vec3 shirt{ 0.85f, 0.25f, 0.30f };
    Vec3 pants{ 0.25f, 0.35f, 0.65f };
};

class Climber {
public:
    void init(VerletWorld* world, const HeightField* hf, const Vec3& spawn);
    void shutdown(VerletWorld* world);

    // input each fixed step: move dir in world space, wantJump, look yaw
    void control(const Vec3& moveDir, bool wantJump, bool grabL, bool grabR,
                 const Vec3& grabPointL, const Vec3& grabPointR);

    void simulate(float dt);          // called by the game after VerletWorld::step

    Vec3 pelvisPos() const { return pelvis_; }
    Vec3 headPos() const;
    bool grounded() const { return groundedTimer_ > 0; }

    // render helpers: oriented boxes (yAxis = bone direction, zHint = facing hint)
    struct PartBox { Vec3 center; Vec3 half; Vec3 color; Vec3 yAxis; Vec3 zHint; };
    int collectParts(PartBox* out) const;

private:
    void updateBalance(float dt);
    void updateFeet(float dt);
    void applyMovement(const Vec3& moveDir, bool wantJump, float dt);
    void updateGrab(bool grabL, bool grabR, const Vec3& pl, const Vec3& pr);

    VerletWorld* w_ = nullptr;
    const HeightField* hf_ = nullptr;

    Vec3 pelvis_{};
    Vec3 pelvisVel_{};
    float yaw_ = 0.f;

    // limb particle indices in the verlet world
    int head_ = -1, shoulderL_ = -1, shoulderR_ = -1;
    int handL_ = -1, handR_ = -1, elbowL_ = -1, elbowR_ = -1;
    int hipL_ = -1, hipR_ = -1, footL_ = -1, footR_ = -1, kneeL_ = -1, kneeR_ = -1;

    // grabbing
    bool grabbingL_ = false, grabbingR_ = false;
    Vec3 grabAnchorL_{}, grabAnchorR_{};

    // cached input intents (set by control(), consumed by simulate())
    Vec3 moveDir_{};
    bool wantJump_ = false;
    bool grabL_ = false, grabR_ = false;
    Vec3 grabPointL_{}, grabPointR_{};

    // procedural stepping
    struct StepState {
        bool stepping = false;
        float t = 0.f;
        Vec3 from{}, to{}, anchor{};
    };
    StepState stepL_, stepR_;

    float groundedTimer_ = 0.f;
    float stamina_ = 100.f;
    ClimberVisual vis_;
};

} // namespace bip
