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

    stepL_.anchor = pelvis_ + Vec3{-0.13f, 0, 0};
    stepR_.anchor = pelvis_ + Vec3{ 0.13f, 0, 0};
}

void Climber::shutdown(VerletWorld* w) {
    (void)w; // particles live in world's pools; fine to leak indices for now
}

void Climber::respawn(const Vec3& p) {
    float gy = hf_->heightAt(p.x, p.z);
    pelvis_ = Vec3{p.x, std::max(p.y, gy + kPelvisHeight), p.z};
    pelvisVel_ = Vec3{};
    // re-seat limbs around the new pelvis
    auto seat = [&](int idx, const Vec3& off) {
        Particle& q = w_->particles_[idx];
        q.pos = pelvis_ + off;
        q.prev = q.pos;
    };
    seat(head_,      {0, 0.62f, 0});
    seat(shoulderL_, {-0.18f, 0.48f, 0}); seat(shoulderR_, { 0.18f, 0.48f, 0});
    seat(elbowL_,    {-0.26f, 0.22f, 0}); seat(elbowR_,    { 0.26f, 0.22f, 0});
    seat(handL_,     {-0.28f,-0.02f, 0}); seat(handR_,     { 0.28f,-0.02f, 0});
    seat(hipL_,      {-0.10f,-0.05f, 0}); seat(hipR_,      { 0.10f,-0.05f, 0});
    seat(kneeL_,     {-0.11f,-0.45f, 0}); seat(kneeR_,     { 0.11f,-0.45f, 0});
    seat(footL_,     {-0.12f,-0.88f, 0}); seat(footR_,     { 0.12f,-0.88f, 0});
    stepL_.stepping = stepR_.stepping = false;
    stepL_.anchor = pelvis_ + Vec3{-0.13f, 0, 0};
    stepR_.anchor = pelvis_ + Vec3{ 0.13f, 0, 0};
    grabbingL_ = grabbingR_ = false;
}

Vec3 Climber::headPos() const { return w_->particlePos(head_); }

void Climber::teleportPelvis(const Vec3& p) {
    // net puppet: move pelvis, drag limb particles with it (keeps pose, no snap)
    Vec3 delta = p - pelvis_;
    pelvis_ = p;
    pelvisVel_ = delta * 60.f;   // implied velocity for smooth continuation
    for (int i = 0; i < 12; ++i) {   // our 12 particles were created first
        Particle& q = w_->particles_[i];
        q.pos = q.pos + delta;
        q.prev = q.pos;
    }
}

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

    // pelvis vs boxes: kinematic sphere pushes through NOTHING; shoves boxes
    for (int bi = 0; bi < (int)w_->boxes_.size(); ++bi) {
        Vec3 push = w_->collideSphereWithBox(pelvis_, 0.34f, bi, &pelvisVel_);
        if (!(push.x == 0.f && push.y == 0.f && push.z == 0.f))
            pelvis_ = pelvis_ + push;
    }

    // hard floor: never sink into terrain
    float gy = hf_->heightAt(pelvis_.x, pelvis_.z) + 0.45f;
    if (pelvis_.y < gy) {
        pelvis_.y = gy;
        if (pelvisVel_.y < 0) pelvisVel_.y *= -0.1f;
    }
}

void Climber::updateGrab(bool grabL, bool grabR, const Vec3& pl, const Vec3& pr) {
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
    updateFeet(dt);
    updateGrab(grabL_, grabR_, grabPointL_, grabPointR_);
    wantJump_ = false; // one-shot
}

void Climber::updateFeet(float dt) {
    // Procedural walk: feet stick to ground; when pelvis drifts too far from
    // a planted foot, that foot steps toward a predicted spot.
    float speed = length(Vec3{pelvisVel_.x, 0, pelvisVel_.z});
    bool canStep = grounded() && speed > 0.6f && !grabbingL_ && !grabbingR_;

    for (int side = 0; side < 2; ++side) {
        StepState& st = side ? stepR_ : stepL_;
        int footIdx = side ? footR_ : footL_;
        Particle& f = w_->particles_[footIdx];

        if (!canStep) {
            // reset any mid-step pose smoothly (verlet settles it)
            st.stepping = false;
            continue;
        }

        if (!st.stepping) {
            float drift = length(Vec3{pelvis_.x - st.anchor.x, 0, pelvis_.z - st.anchor.z});
            float otherDrift = length(Vec3{
                pelvis_.x - (side ? stepL_.anchor.x : stepR_.anchor.x), 0,
                pelvis_.z - (side ? stepL_.anchor.z : stepR_.anchor.z)});
            // step when drifted far AND the other foot is planted (alternating gait)
            if (drift > 0.55f && !(side ? stepL_.stepping : stepR_.stepping)
                && drift > otherDrift * 0.6f) {
                st.stepping = true;
                st.t = 0.f;
                st.from = f.pos;
                Vec3 dir = normalize(Vec3{pelvisVel_.x, 0, pelvisVel_.z});
                Vec3 lateral = normalize(cross(Vec3{0,1,0}, dir));
                Vec3 want = pelvis_ + dir * 0.42f + lateral * (side ? 0.13f : -0.13f);
                st.to = Vec3{want.x, hf_->heightAt(want.x, want.z), want.z};
            }
        } else {
            st.t += dt * (3.5f + speed * 0.5f);
            if (st.t >= 1.f) {
                st.t = 1.f;
                st.stepping = false;
                st.anchor = st.to;
            }
            float e = st.t * st.t * (3.f - 2.f * st.t);   // smoothstep
            Vec3 p = st.from + (st.to - st.from) * e;
            p.y += sinf(st.t * 3.14159f) * 0.16f;          // foot lift
            f.pos = p;
            f.prev = p;                                     // kinematic during swing
        }
    }
}

int Climber::collectParts(PartBox* out) const {
    int n = 0;
    // add: center, half-extents, color, bone axis (box local Y), facing hint (local Z)
    auto add = [&](const Vec3& c, const Vec3& h, const Vec3& col,
                   const Vec3& yAxis, const Vec3& zHint) {
        out[n].center = c; out[n].half = h; out[n].color = col;
        out[n].yAxis = normalize(yAxis);
        Vec3 x = normalize(cross(out[n].yAxis, zHint));
        out[n].zHint = normalize(cross(x, out[n].yAxis));
        ++n;
    };
    auto P = [&](int i){ return w_->particlePos(i); };

    Vec3 shirt = vis_.shirt, pants = vis_.pants, skin = vis_.skin;

    // --- torso: oriented along spine, faces movement direction
    Vec3 shL = P(shoulderL_), shR = P(shoulderR_);
    Vec3 hipL = P(hipL_),     hipR = P(hipR_);
    Vec3 shMid = (shL + shR) * 0.5f;
    Vec3 hipMid = (hipL + hipR) * 0.5f;
    float torsoLen = length(shMid - hipMid) + 0.10f;
    Vec3 faceDir = normalize(shR - shL);                       // left->right = forward hint
    add((shMid + hipMid) * 0.5f,
        Vec3{0.16f, torsoLen * 0.5f, 0.10f}, shirt,
        shMid - hipMid, faceDir);

    // --- head: upright-ish but tilts with neck
    Vec3 headP = P(head_);
    add(headP, Vec3{0.115f, 0.115f, 0.115f}, skin,
        headP - shMid, faceDir);

    // --- limbs: boxes aligned to bones + joint cubes at knees/elbows/shoulders/hips/hands/feet
    auto seg = [&](int a, int b, Vec3 col, float thick) {
        Vec3 pa = P(a), pb = P(b);
        Vec3 d = pb - pa;
        float len = length(d);
        if (len < 1e-4f) return;
        add((pa+pb)*0.5f, Vec3{thick, len*0.5f + thick*0.6f, thick},
            col, d, cross(normalize(d), faceDir));
    };
    auto joint = [&](int i, Vec3 col, float r) {
        add(P(i), Vec3{r,r,r}, col, Vec3{0,1,0}, faceDir);
    };

    seg(shoulderL_, elbowL_, shirt, 0.058f); joint(elbowL_, shirt, 0.062f);
    seg(elbowL_, handL_, skin, 0.050f);      joint(handL_, skin, 0.055f);
    seg(shoulderR_, elbowR_, shirt, 0.058f); joint(elbowR_, shirt, 0.062f);
    seg(elbowR_, handR_, skin, 0.050f);      joint(handR_, skin, 0.055f);
    joint(shoulderL_, shirt, 0.07f);         joint(shoulderR_, shirt, 0.07f);

    seg(hipL_, kneeL_, pants, 0.062f);       joint(kneeL_, pants, 0.065f);
    seg(kneeL_, footL_, pants, 0.055f);      joint(footL_, skin, 0.058f);
    seg(hipR_, kneeR_, pants, 0.062f);       joint(kneeR_, pants, 0.065f);
    seg(kneeR_, footR_, pants, 0.055f);      joint(footR_, skin, 0.058f);
    joint(hipL_, pants, 0.07f);              joint(hipR_, pants, 0.07f);

    return n;   // ~23 parts
}

} // namespace bip
