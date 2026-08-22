#include "game/player/climber.h"
#include "world/heightfield.h"
#include <cmath>
#include <algorithm>

namespace bip {

static const float kPelvisHeight   = 0.95f;  // standing pelvis Y above ground
static const float kWalkSpeed      = 4.2f;
static const float kJumpVel        = 7.0f;
static const float kGrabRange      = 1.9f;

void Climber::init(VerletWorld* w, const HeightField* hf, const Vec3& spawn) {
    w_ = w; hf_ = hf;
    float gy = hf->heightAt(spawn.x, spawn.z);
    pelvis_ = Vec3{spawn.x, gy + kPelvisHeight, spawn.z};
    pelvisVel_ = Vec3{};

    const float m = 1.f; // limb particle inverse mass
    auto P = [&](const Vec3& p){ return w->addParticle(p, vis_.limbR, m); };
    head_      = P(pelvis_ + Vec3{0, 0.62f, 0});
    shoulderL_ = P(pelvis_ + Vec3{-0.18f, 0.48f, 0});
    shoulderR_ = P(pelvis_ + Vec3{ 0.18f, 0.48f, 0});
    elbowL_    = P(pelvis_ + Vec3{-0.26f, 0.22f, 0});
    elbowR_    = P(pelvis_ + Vec3{ 0.26f, 0.22f, 0});
    handL_     = P(pelvis_ + Vec3{-0.28f, -0.02f, 0});
    handR_     = P(pelvis_ + Vec3{ 0.28f, -0.02f, 0});
    hipL_      = P(pelvis_ + Vec3{-0.10f, -0.05f, 0});
    hipR_      = P(pelvis_ + Vec3{ 0.10f, -0.05f, 0});
    kneeL_     = P(pelvis_ + Vec3{-0.11f, -0.45f, 0});
    kneeR_     = P(pelvis_ + Vec3{ 0.11f, -0.45f, 0});
    footL_     = P(pelvis_ + Vec3{-0.12f, -0.88f, 0});
    footR_     = P(pelvis_ + Vec3{ 0.12f, -0.88f, 0});

    auto C = [&](int a, int b, float s){ w->addConstraint(a, b, s); };
    // torso rig
    C(head_, shoulderL_, 1.f); C(head_, shoulderR_, 1.f);
    C(shoulderL_, hipL_, 1.f); C(shoulderR_, hipR_, 1.f);
    C(shoulderL_, hipR_, 0.8f); C(shoulderR_, hipL_, 0.8f); // anti-fold
    // arms (with slight bend resistance)
    C(shoulderL_, elbowL_, 1.f); C(elbowL_, handL_, 1.f); C(shoulderL_, handL_, 0.25f);
    C(shoulderR_, elbowR_, 1.f); C(elbowR_, handR_, 1.f); C(shoulderR_, handR_, 0.25f);
    // legs
    C(hipL_, kneeL_, 1.f); C(kneeL_, footL_, 1.f); C(hipL_, footL_, 0.2f);
    C(hipR_, kneeR_, 1.f); C(kneeR_, footR_, 1.f); C(hipR_, footR_, 0.2f);
    // hips follow pelvis via soft constraints to a pinned proxy? simpler:
    // pin hips weakly to pelvis by making pelvis drive them in updateBalance.
}

void Climber::shutdown(VerletWorld* w) {
    (void)w; // particles live in world's pools; fine to leak indices for now
}

Vec3 Climber::headPos() const { return w_->particlePos(head_); }

// pelvis is the "brain": we integrate it manually with crisp control,
// then drag limb roots (hips/shoulders) toward it every step.
void Climber::updateBalance(float dt) {
    // stand height above terrain
    float gy = hf_->heightAt(pelvis_.x, pelvis_.z);
    float targetY = gy + kPelvisHeight;

    // legs push up if below stand height and near ground
    if (pelvis_.y < targetY) {
        float push = std::min(targetY - pelvis_.y, 6.f * dt);
        pelvis_.y += push;
        if (pelvisVel_.y < 0) pelvisVel_.y = 0;
        groundedTimer_ = 0.15f;
    } else {
        groundedTimer_ -= dt;
    }

    // drag limb roots toward their offsets from pelvis (spring)
    auto drag = [&](int idx, const Vec3& offset, float k) {
        Particle& p = w_->particles_[idx];
        Vec3 want = pelvis_ + offset;
        p.pos = p.pos + (want - p.pos) * k;
    };
    float k = std::min(1.f, 14.f * dt);
    drag(hipL_, Vec3{-0.10f, -0.05f, 0}, k);
    drag(hipR_, Vec3{ 0.10f, -0.05f, 0}, k);
    drag(shoulderL_, Vec3{-0.16f, 0.44f, 0}, k*0.9f);
    drag(shoulderR_, Vec3{ 0.16f, 0.44f, 0}, k*0.9f);
    drag(head_, Vec3{0, 0.58f, 0}, k);
}

void Climber::applyMovement(const Vec3& moveDir, bool wantJump, float dt) {
    Vec3 accel{};
    float speed = kWalkSpeed * (grabbingL_ || grabbingR_ ? 0.35f : 1.f);
    if (moveDir.x != 0 || moveDir.z != 0) {
        Vec3 dir = normalize(moveDir);
        accel = dir * speed;
        yaw_ = atan2f(dir.x, dir.z);
    }
    pelvisVel_.x += (accel.x - pelvisVel_.x) * std::min(1.f, 10.f*dt);
    pelvisVel_.z += (accel.z - pelvisVel_.z) * std::min(1.f, 10.f*dt);

    // gravity on pelvis
    pelvisVel_.y -= 20.f * dt;

    if (wantJump && grounded()) {
        pelvisVel_.y = kJumpVel;
        groundedTimer_ = 0.f;
    }

    pelvis_ = pelvis_ + pelvisVel_ * dt;

    // hard floor: never sink into terrain
    float gy = hf_->heightAt(pelvis_.x, pelvis_.z) + 0.45f;
    if (pelvis_.y < gy) {
        pelvis_.y = gy;
        if (pelvisVel_.y < 0) pelvisVel_.y *= -0.1f;
    }
}

void Climber::updateGrab(bool grabL, bool grabR, const Vec3& pl, const Vec3& pr) {
    // start grabs
    if (grabL && !grabbingL_) {
        Vec3 handPos = w_->particlePos(handL_);
        Vec3 d = pl - handPos;
        if (length(d) < kGrabRange && length(d) > 0.01f) {
            grabbingL_ = true;
            grabAnchorL_ = handPos + d; // grab point
            stamina_ -= 2.f;
        }
    }
    if (grabR && !grabbingR_) {
        Vec3 handPos = w_->particlePos(handR_);
        Vec3 d = pr - handPos;
        if (length(d) < kGrabRange && length(d) > 0.01f) {
            grabbingR_ = true;
            grabAnchorR_ = handPos + d;
            stamina_ -= 2.f;
        }
    }
    // release
    if (!grabL) grabbingL_ = false;
    if (!grabR) grabbingR_ = false;

    // while grabbing: hands are pulled hard to anchors, body hangs from them
    if (grabbingL_) {
        Particle& h = w_->particles_[handL_];
        h.pos = h.pos + (grabAnchorL_ - h.pos) * std::min(1.f, 20.f * 0.016f);
        // body swing: pull pelvis toward anchor
        pelvisVel_ = pelvisVel_ + (grabAnchorL_ - pelvis_) * 0.9f * 0.016f * 60.f * 0.02f;
        stamina_ -= 6.f * 0.016f;
    }
    if (grabbingR_) {
        Particle& h = w_->particles_[handR_];
        h.pos = h.pos + (grabAnchorR_ - h.pos) * std::min(1.f, 20.f * 0.016f);
        pelvisVel_ = pelvisVel_ + (grabAnchorR_ - pelvis_) * 0.9f * 0.016f * 60.f * 0.02f;
        stamina_ -= 6.f * 0.016f;
    }
    stamina_ = std::max(0.f, std::min(100.f, stamina_));
}

void Climber::control(const Vec3& moveDir, bool wantJump, bool grabL, bool grabR,
                      const Vec3& grabPointL, const Vec3& grabPointR) {
    moveDir_ = moveDir;
    wantJump_ = wantJump;
    grabL_ = grabL; grabR_ = grabR;
    grabPointL_ = grabPointL; grabPointR_ = grabPointR;
}

void Climber::simulate(float dt) {
    applyMovement(moveDir_, wantJump_, dt);
    updateBalance(dt);
    updateGrab(grabL_, grabR_, grabPointL_, grabPointR_);
    wantJump_ = false; // one-shot
}

int Climber::collectParts(PartBox* out) const {
    int n = 0;
    auto add = [&](const Vec3& c, const Vec3& h, const Vec3& col){ 
        out[n].center = c; out[n].half = h; out[n].color = col; ++n; };

    Vec3 shirt = vis_.shirt, pants = vis_.pants, skin = vis_.skin;

    // torso between mid-shoulders and mid-hips
    Vec3 shMid = (w_->particlePos(shoulderL_) + w_->particlePos(shoulderR_)) * 0.5f;
    Vec3 hipMid = (w_->particlePos(hipL_) + w_->particlePos(hipR_)) * 0.5f;
    Vec3 torsoC = (shMid + hipMid) * 0.5f;
    add(torsoC, Vec3{0.16f, length(shMid-hipMid)*0.5f + 0.06f, 0.10f}, shirt);

    add(w_->particlePos(head_), Vec3{0.11f,0.11f,0.11f}, skin);

    // limbs as small boxes at joints (v1: chunky segments)
    auto seg = [&](int a, int b, Vec3 col){
        Vec3 pa = w_->particlePos(a), pb = w_->particlePos(b);
        add((pa+pb)*0.5f, Vec3{0.055f, length(pb-pa)*0.5f+0.03f, 0.055f}, col);
    };
    seg(shoulderL_, elbowL_, shirt); seg(elbowL_, handL_, skin);
    seg(shoulderR_, elbowR_, shirt); seg(elbowR_, handR_, skin);
    seg(hipL_, kneeL_, pants);       seg(kneeL_, footL_, skin);
    seg(hipR_, kneeR_, pants);       seg(kneeR_, footR_, skin);
    return n;
}

} // namespace bip
