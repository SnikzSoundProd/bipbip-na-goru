#include "physics/verlet.h"
#include "world/heightfield.h"
#include <cmath>
#include <algorithm>

namespace bip {

static const float kSkin = 0.001f;

void VerletWorld::init(const HeightField* hf) {
    hf_ = hf;
}

int VerletWorld::addParticle(const Vec3& pos, float radius, float invMass) {
    Particle p;
    p.pos = pos; p.prev = pos;
    p.radius = radius; p.invMass = invMass;
    particles_.push_back(p);
    return (int)particles_.size() - 1;
}

int VerletWorld::addConstraint(int a, int b, float stiffness) {
    DistanceConstraint c;
    c.a = a; c.b = b;
    c.restLen = length(particles_[a].pos - particles_[b].pos);
    c.stiffness = stiffness;
    constraints_.push_back(c);
    return (int)constraints_.size() - 1;
}

int VerletWorld::addBox(const Vec3& pos, float hx, float hy, float hz) {
    BoxProp b;
    b.pos = pos; b.vel = Vec3();
    b.yaw = 0.f; b.yawVel = 0.f;
    b.hx = hx; b.hy = hy; b.hz = hz;
    boxes_.push_back(b);
    return (int)boxes_.size() - 1;
}

void VerletWorld::setParticlePinned(int i, bool pinned) {
    particles_[i].invMass = pinned ? 0.f : 1.f;
}

void VerletWorld::addParticleForce(int i, const Vec3& f) {
    // verlet: impulse ~ position nudge; caller scales appropriately
    Particle& p = particles_[i];
    if (p.invMass > 0.f) p.pos = p.pos + f;
}

void VerletWorld::integrate(float dt) {
    for (auto& p : particles_) {
        if (p.invMass <= 0.f) continue;
        Vec3 vel = (p.pos - p.prev) * 0.985f;   // damping
        p.prev = p.pos;
        p.pos = p.pos + vel + gravity_ * (dt * dt);
    }
}

void VerletWorld::solveConstraints() {
    for (int it = 0; it < iterations_; ++it) {
        for (auto& c : constraints_) {
            Particle& a = particles_[c.a];
            Particle& b = particles_[c.b];
            float wA = a.invMass, wB = b.invMass;
            float wSum = wA + wB;
            if (wSum <= 0.f) continue;
            Vec3 delta = b.pos - a.pos;
            float len = length(delta);
            if (len < 1e-6f) continue;
            float diff = (len - c.restLen) / len * c.stiffness;
            a.pos = a.pos + delta * (wA / wSum * diff);
            b.pos = b.pos - delta * (wB / wSum * diff);
        }
        collideHeightField();       // interleave so constraints don't fight the ground
    }
}

void VerletWorld::collideHeightField() {
    if (!hf_) return;
    for (auto& p : particles_) {
        if (p.invMass <= 0.f) continue;
        float h = hf_->heightAt(p.pos.x, p.pos.z);
        float floorY = h + p.radius;
        if (p.pos.y < floorY) {
            // push out along Y and kill inward velocity (simple friction via prev lerp)
            p.pos.y = floorY + kSkin;
            Vec3 vel = p.pos - p.prev;
            vel.y *= -0.15f;                       // restitution
            vel.x *= 0.75f; vel.z *= 0.75f;        // ground friction
            p.prev = p.pos - vel;
        }
    }
}

// --- box props: euler integration + yaw-only rotation -----------------------
static void yawAxes(float yaw, Vec3& right, Vec3& fwd) {
    right = { cosf(yaw), 0.f, -sinf(yaw) };
    fwd   = { sinf(yaw), 0.f,  cosf(yaw) };
}

static bool sphereOBB(const Vec3& sc, float r,
                      const Vec3& bc, const Vec3& right, const Vec3& fwd,
                      float hx, float hy, float hz, Vec3& outPush) {
    Vec3 d = sc - bc;
    float lx = dot(d, right);
    float lz = dot(d, fwd);
    float ly = d.y;
    float cx = std::max(-hx, std::min(hx, lx));
    float cy = std::max(-hy, std::min(hy, ly));
    float cz = std::max(-hz, std::min(hz, lz));
    Vec3 closestLocal{ cx, cy, cz };
    Vec3 closest = bc + right * closestLocal.x + fwd * closestLocal.z + Vec3{0, cy, 0};
    Vec3 n = sc - closest;
    float dist2 = dot(n, n);
    if (dist2 > r*r) return false;
    float dist = sqrtf(dist2);
    if (dist < 1e-5f) { outPush = Vec3{0, r, 0}; return true; } // deep inside: pop up
    outPush = n * ((r - dist) / dist);
    return true;
}

void VerletWorld::collideBoxesParticles() {
    for (auto& b : boxes_) {
        Vec3 right, fwd;
        yawAxes(b.yaw, right, fwd);
        for (auto& p : particles_) {
            if (p.invMass <= 0.f) continue;
            Vec3 push;
            if (sphereOBB(p.pos, p.radius, b.pos, right, fwd, b.hx, b.hy, b.hz, push)) {
                p.pos = p.pos + push;
                p.prev = p.prev + push * 0.5f;      // half-kill velocity change
                // reaction on the box
                float m = p.invMass > 0 ? 1.f/p.invMass : 0.f;
                b.vel = b.vel - push * (0.35f * m);
                b.yawVel += (push.x * fwd.z - push.z * fwd.x) * 0.02f;
            }
        }
    }
}

Vec3 VerletWorld::collideSphereWithBox(const Vec3& posIn, float radius, int boxIdx,
                                       Vec3* velInOut) {
    BoxProp& b = boxes_[boxIdx];
    Vec3 right, fwd;
    yawAxes(b.yaw, right, fwd);
    Vec3 push;
    if (!sphereOBB(posIn, radius, b.pos, right, fwd, b.hx, b.hy, b.hz, push))
        return Vec3{};

    // box gets shoved by the sphere's motion
    Vec3& v = *velInOut;
    b.vel = b.vel + push * 6.f + v * 0.25f;
    b.yawVel += (push.x * fwd.z - push.z * fwd.x) * 0.35f;
    // dampen the sphere's velocity along the push (can't tunnel through)
    float vn = dot(v, normalize(push));
    if (vn < 0.f) v = v - normalize(push) * vn;
    return push;
}

void VerletWorld::collideBoxesBoxes() {
    // yaw-only OBB vs OBB: approximate each as a sphere of radius = avg half extent
    // for the broad pass, then resolve with the sphere-OBB routine both ways.
    for (size_t i = 0; i < boxes_.size(); ++i) {
        for (size_t j = i + 1; j < boxes_.size(); ++j) {
            BoxProp& A = boxes_[i];
            BoxProp& B = boxes_[j];
            float ra = (A.hx + A.hy + A.hz) * 0.62f;
            float rb = (B.hx + B.hy + B.hz) * 0.62f;
            Vec3 d = B.pos - A.pos;
            float dist2 = dot(d, d);
            float rsum = ra + rb;
            if (dist2 > rsum * rsum || dist2 < 1e-8f) continue;
            float dist = sqrtf(dist2);
            Vec3 n = d * (1.f / dist);
            float overlap = rsum - dist;
            // split by "mass" (volume-ish)
            float ma = A.hx * A.hy * A.hz, mb = B.hx * B.hy * B.hz;
            float wa = mb / (ma + mb), wb = ma / (ma + mb);
            A.pos = A.pos - n * (overlap * wa);
            B.pos = B.pos + n * (overlap * wb);
            // exchange a bit of velocity
            Vec3 rel = B.vel - A.vel;
            float vn = dot(rel, n);
            if (vn < 0.f) {
                Vec3 imp = n * (vn * 0.6f);
                A.vel = A.vel + imp * wa;
                B.vel = B.vel - imp * wb;
            }
        }
    }
}

void VerletWorld::integrateBoxes(float dt) {
    for (auto& b : boxes_) {
        b.vel = b.vel + gravity_ * dt;
        b.vel = b.vel * 0.998f;
        b.pos = b.pos + b.vel * dt;
        b.yawVel *= 0.99f;
        b.yaw += b.yawVel * dt;
    }
}

void VerletWorld::collideBoxesTerrain() {
    if (!hf_) return;
    for (auto& b : boxes_) {
        float h = hf_->heightAt(b.pos.x, b.pos.z);
        float bottom = b.pos.y - std::max(b.hy, 0.4f*(b.hx+b.hz));
        if (bottom < h) {
            b.pos.y += h - bottom + kSkin;
            if (b.vel.y < 0.f) b.vel.y = -b.vel.y * 0.25f;
            b.vel.x *= 0.92f; b.vel.z *= 0.92f;
            b.yawVel *= 0.95f;
        }
    }
}

void VerletWorld::step(float dt) {
    integrate(dt);
    integrateBoxes(dt);
    solveConstraints();          // includes heightfield for particles
    collideBoxesTerrain();
    collideBoxesParticles();
    collideBoxesBoxes();
    collideHeightField();        // final settle pass
}

} // namespace bip
