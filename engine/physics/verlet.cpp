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
    b.rot = Quat{}; b.angVel = Vec3();
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

// --- rigid body boxes: full 3D rotation -------------------------------------
static void boxAxes(const Quat& q, Vec3& ax, Vec3& ay, Vec3& az) {
    // unit box axes = rotated basis vectors
    ax = rotate(q, Vec3{1,0,0});
    ay = rotate(q, Vec3{0,1,0});
    az = rotate(q, Vec3{0,0,1});
}

// sphere vs fully-oriented box
static bool sphereOBB(const Vec3& sc, float r,
                      const Vec3& bc, const Vec3 ax[3],
                      const float he[3], Vec3& outPush) {
    Vec3 d = sc - bc;
    float l[3] = { dot(d, ax[0]), dot(d, ax[1]), dot(d, ax[2]) };
    float c[3];
    for (int i = 0; i < 3; ++i) c[i] = std::max(-he[i], std::min(he[i], l[i]));
    Vec3 closest = bc;
    for (int i = 0; i < 3; ++i)
        closest = closest + ax[i] * (c[i] - l[i]);   // move from sphere-proj to clamp
    Vec3 n = sc - closest;
    float dist2 = dot(n, n);
    if (dist2 > r*r) return false;
    float dist = sqrtf(dist2);
    outPush = dist < 1e-5f ? Vec3{0, r, 0} : n * ((r - dist) / dist);
    return true;
}

// velocity of box material point at world offset r from COM
static Vec3 pointVel(const BoxProp& b, const Vec3& r) {
    return b.vel + cross(b.angVel, r);
}

// apply impulse j along n at world offset r (updates linear + angular)
static void applyImpulse(BoxProp& b, const Vec3& n, float j, const Vec3& r) {
    b.vel = b.vel + n * j;                 // mass folded into j by caller
    Vec3 torque = cross(r, n * j);
    // local-frame angular response with diagonal inertia
    float m = b.hx * b.hy * b.hz;
    Vec3 Ilocal{
        m / 3.f * (b.hy*b.hy + b.hz*b.hz),
        m / 3.f * (b.hx*b.hx + b.hz*b.hz),
        m / 3.f * (b.hx*b.hx + b.hy*b.hy)
    };
    Vec3 tl = rotate(b.rot.conj(), torque);
    Vec3 dw{ tl.x / Ilocal.x, tl.y / Ilocal.y, tl.z / Ilocal.z };
    b.angVel = b.angVel + rotate(b.rot, dw);
}

void VerletWorld::collideBoxesParticles() {
    for (auto& b : boxes_) {
        Vec3 ax[3]; boxAxes(b.rot, ax[0], ax[1], ax[2]);
        float he[3] = { b.hx, b.hy, b.hz };
        for (auto& p : particles_) {
            if (p.invMass <= 0.f) continue;
            Vec3 push;
            if (!sphereOBB(p.pos, p.radius, b.pos, ax, he, push)) continue;

            Vec3 cp = p.pos - push;                    // approx contact point
            Vec3 r = cp - b.pos;
            Vec3 n = normalize(push);                  // away from box
            Vec3 bv = pointVel(b, r);
            Vec3 pv = (p.pos - p.prev);
            Vec3 relv = bv - pv;                       // box sees particle approaching
            float vn = dot(relv, n);

            p.pos = p.pos + push;
            p.prev = p.prev + push * 0.4f;

            if (vn < 0.f) {
                // impulse pushes box away from particle (mass-scaled)
                float pm = p.invMass > 0.f ? 1.f / p.invMass : 1000.f;
                float bm = b.hx * b.hy * b.hz * 8.f;
                float j = -vn * (pm * bm) / (pm + bm) * 1.4f;
                applyImpulse(b, -n, j / bm, r);
            }
        }
    }
}

Vec3 VerletWorld::collideSphereWithBox(const Vec3& posIn, float radius, int boxIdx,
                                       Vec3* velInOut) {
    BoxProp& b = boxes_[boxIdx];
    Vec3 ax[3]; boxAxes(b.rot, ax[0], ax[1], ax[2]);
    float he[3] = { b.hx, b.hy, b.hz };
    Vec3 push;
    if (!sphereOBB(posIn, radius, b.pos, ax, he, push))
        return Vec3{};

    Vec3 cp = posIn - push;
    Vec3 r = cp - b.pos;
    Vec3 n = normalize(push);
    Vec3 bv = pointVel(b, r);
    Vec3& v = *velInOut;
    float vn = dot(v - bv, n);

    // positional separation for the kinematic sphere
    float vnBox = dot(b.vel, n);
    if (vn < vnBox) {
        // relative approach speed -> shove box with player momentum share
        float bm = b.hx * b.hy * b.hz * 8.f;
        float pm = 60.f;                                // player feels heavy
        float jrel = (vnBox - vn) * (pm * bm) / (pm + bm);
        applyImpulse(b, n, jrel / bm, r);
    }
    // kill player velocity component INTO the box
    float into = dot(v, n) - vnBox;
    if (into < 0.f) v = v - n * into;
    return push;
}

void VerletWorld::collideBoxesBoxes() {
    // broad-phase spheres; narrow resolution via separating-axis-ish push.
    // Full OBB-OBB SAT is Phase 5 polish; sphere approx reads fine in motion.
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
            float ma = A.hx * A.hy * A.hz, mb = B.hx * B.hy * B.hz;
            float wa = mb / (ma + mb), wb = ma / (ma + mb);
            A.pos = A.pos - n * (overlap * wa);
            B.pos = B.pos + n * (overlap * wb);
            // contact-point impulse both ways
            Vec3 cp = A.pos + n * (ra * 1.f);
            Vec3 ra_ = cp - A.pos, rb_ = cp - B.pos;
            Vec3 relv = pointVel(B, rb_) - pointVel(A, ra_);
            float vn = dot(relv, n);
            if (vn < 0.f) {
                float total = ma + mb;
                float jA = -vn * (mb / total), jB = -vn * (ma / total);
                applyImpulse(A, -n, jA / ma, ra_);
                applyImpulse(B,  n, jB / mb, rb_);
            }
        }
    }
}

void VerletWorld::integrateBoxes(float dt) {
    for (auto& b : boxes_) {
        b.vel = b.vel + gravity_ * dt;
        b.vel = b.vel * 0.999f;
        b.pos = b.pos + b.vel * dt;
        // quaternion integration: dq = 0.5 * omega*q * dt
        Vec3 w = b.angVel * 0.5f;
        Quat dw{ w.x, w.y, w.z, 0.f };
        Quat dq = dw * b.rot;
        b.rot = Quat{ b.rot.x + dq.x*dt, b.rot.y + dq.y*dt,
                      b.rot.z + dq.z*dt, b.rot.w + dq.w*dt }.normalized();
        b.angVel = b.angVel * 0.996f;   // slight rotational damping
    }
}

void VerletWorld::collideBoxesTerrain() {
    if (!hf_) return;
    for (auto& b : boxes_) {
        Vec3 ax[3]; boxAxes(b.rot, ax[0], ax[1], ax[2]);
        float he[3] = { b.hx, b.hy, b.hz };

        // terrain normal at box position (finite differences)
        float e = 0.35f;
        float hC = hf_->heightAt(b.pos.x, b.pos.z);
        float hx1 = hf_->heightAt(b.pos.x + e, b.pos.z);
        float hx0 = hf_->heightAt(b.pos.x - e, b.pos.z);
        float hz1 = hf_->heightAt(b.pos.x, b.pos.z + e);
        float hz0 = hf_->heightAt(b.pos.x, b.pos.z - e);
        Vec3 tn = normalize(Vec3{ -(hx1-hx0)/(2*e), 1.f, -(hz1-hz0)/(2*e) });

        // check all 8 corners as contact candidates
        float maxPen = 0.f;
        int contacts = 0;
        for (int cx = -1; cx <= 1; cx += 2)
        for (int cy = -1; cy <= 1; cy += 2)
        for (int cz = -1; cz <= 1; cz += 2) {
            Vec3 rc = ax[0]*(he[0]*cx) + ax[1]*(he[1]*cy) + ax[2]*(he[2]*cz);
            Vec3 wp = b.pos + rc;
            float h = hf_->heightAt(wp.x, wp.z);
            float pen = h - wp.y;
            if (pen <= 0.f) continue;
            ++contacts;
            maxPen = std::max(maxPen, pen);

            // contact velocity at this corner
            Vec3 v = pointVel(b, rc);
            float vn = dot(v, tn);
            if (vn < 0.f) {
                // impulse with restitution + friction
                float e_rest = (vn < -2.f) ? 0.28f : 0.05f;  // bounce only on hard hits
                Vec3 tdir = v - tn * vn;                     // tangential part
                float tl = length(tdir);

                // effective mass along normal (approx: corner lever arm)
                float arm = length(rc);
                float m = b.hx * b.hy * b.hz * 8.f;
                float k = 1.f/m + arm*arm * 0.7f;            // rotational coupling fudge
                float jn = -(1.f + e_rest) * vn / k;

                applyImpulse(b, tn, jn / m, rc);
                // Coulomb friction clamp
                if (tl > 1e-4f) {
                    float jt = -tl / k;
                    float maxF = 0.55f * jn;
                    if (-jt > maxF) jt = -maxF;
                    applyImpulse(b, normalize(tdir), jt / m, rc);
                }
            } else if (pen > 0.f) {
                // resting contact: gentle anti-grav kick so it settles, not sinks
                applyImpulse(b, tn, 9.81f * 0.016f, rc);
            }
        }

        if (contacts > 0) {
            // positional correction: lift by deepest penetration
            b.pos.y += maxPen * 0.85f;
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
