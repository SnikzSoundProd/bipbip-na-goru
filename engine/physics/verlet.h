#pragma once
// bipbip physics: custom verlet solver.
// Particles + distance constraints + heightfield/AABB collision.
// Deterministic (fixed timestep, no hidden state) — network-friendly.
#include "core/math.h"
#include <cstdint>
#include <vector>

namespace bip {

class HeightField;

struct Particle {
    Vec3 pos;
    Vec3 prev;      // verlet previous position
    float invMass;  // 0 = static/pinned
    float radius;
};

struct DistanceConstraint {
    uint32_t a, b;
    float restLen;
    float stiffness; // 0..1 per-iteration correction factor
};

// Dynamic box prop (rotating: yaw-only for v1, full quat later)
struct BoxProp {
    Vec3 pos;
    Vec3 vel;
    float yaw, yawVel;
    float hx, hy, hz;   // half extents
};

class VerletWorld {
public:
    void init(const HeightField* hf);
    void shutdown() {}

    // fixed-timestep step
    void step(float dt);

    int addParticle(const Vec3& pos, float radius, float invMass);
    int addConstraint(int a, int b, float stiffness);
    int addBox(const Vec3& pos, float hx, float hy, float hz);

    // particle <-> rigid coupling helpers for the ragdoll later:
    void setParticlePinned(int i, bool pinned);
    Vec3 particlePos(int i) const { return particles_[i].pos; }
    void addParticleForce(int i, const Vec3& f);   // impulse applied as position nudge

    std::vector<Particle>     particles_;
    std::vector<DistanceConstraint> constraints_;
    std::vector<BoxProp>      boxes_;

private:
    void integrate(float dt);
    void solveConstraints();
    void collideHeightField();
    void collideBoxesParticles();
    void integrateBoxes(float dt);
    void collideBoxesTerrain();

    const HeightField* hf_ = nullptr;
    Vec3 gravity_{ 0.f, -9.81f * 2.2f, 0.f };  // gamey gravity (PEAK-like snappy falls)
    int iterations_ = 6;
};

} // namespace bip
