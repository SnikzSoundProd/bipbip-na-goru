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

// Dynamic rigid box: full 3D rotation (quaternion), inertia tensor,
// impulse-based contacts — rolls down slopes, bounces off corners.
struct BoxProp {
    Vec3 pos;
    Vec3 vel;
    Quat rot;            // orientation
    Vec3 angVel;         // world-space angular velocity
    float hx, hy, hz;    // half extents
    uint8_t owner = 0;   // 0=host, 1=client; host still simulates authority
    bool sleeping = false;
    float sleepTimer = 0.f;
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

    // Kinematic sphere (player pelvis) vs box: pushes sphere out, transfers
    // momentum to the box. Returns applied push; updates *velInOut.
    Vec3 collideSphereWithBox(const Vec3& posIn, float radius, int boxIdx,
                              Vec3* velInOut);

    void wake(BoxProp& b);

    // how many boxes are currently asleep (for the profiler)
    int sleepingBoxCount() const {
        int n = 0;
        for (const auto& b : boxes_) if (b.sleeping) ++n;
        return n;
    }

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
    void collideBoxesBoxes();
    void integrateBoxes(float dt);
    void collideBoxesTerrain();

    const HeightField* hf_ = nullptr;
    Vec3 gravity_{ 0.f, -9.81f * 2.2f, 0.f };  // gamey gravity (PEAK-like snappy falls)
    int iterations_ = 6;
};

} // namespace bip
