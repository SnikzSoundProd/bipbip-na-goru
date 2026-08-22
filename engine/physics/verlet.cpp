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
    Particle& p = particles_[i];
    if (p.invMass > 0.f) p.pos = p.pos + f;
}

// --- particles ---------------------------------------------------------------

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
            p.pos.y = floorY + kSkin;
            Vec3 vel = p.pos - p.prev;
            vel.y *= -0.15f;                       // restitution
            vel.x *= 0.75f; vel.z *= 0.75f;        // ground friction
            p.prev = p.pos - vel;
        }
    }
}

// --- rigid body boxes: full 3D rotation -------------------------------------
static void boxAxes(const Quat& q, Vec3* ax) {
    ax[0] = rotate(q, Vec3{1,0,0});
    ax[1] = rotate(q, Vec3{0,1,0});
    ax[2] = rotate(q, Vec3{0,0,1});
}

// sphere vs fully-oriented box; outPush points from box surface to sphere center
static bool sphereOBB(const Vec3& sc, float r,
                      const Vec3& bc, const Vec3* ax,
                      const float* he, Vec3& outPush) {
    Vec3 d = sc - bc;
    float l[3] = { dot(d, ax[0]), dot(d, ax[1]), dot(d, ax[2]) };
    float c[3];
    for (int i = 0; i < 3; ++i) c[i] = std::max(-he[i], std::min(he[i], l[i]));
    Vec3 closest = bc;
    for (int i = 0; i < 3; ++i)
        closest = closest + ax[i] * (c[i] - l[i]);
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

// diagonal inertia in WORLD frame (box axes are principal axes)
static void worldInertia(const BoxProp& b, const Vec3* ax, float m, Vec3& Iout) {
    float ix = m/12.f * ((2*b.hy)*(2*b.hy) + (2*b.hz)*(2*b.hz));
    float iy = m/12.f * ((2*b.hx)*(2*b.hx) + (2*b.hz)*(2*b.hz));
    float iz = m/12.f * ((2*b.hx)*(2*b.hx) + (2*b.hy)*(2*b.hy));
    // transform diag inertia to world: I_world ≈ sum I_i * axis_i ⊗ axis_i (diagonal only)
    // For impulse response we need I⁻¹·τ; with diag-in-world approximation:
    Vec3 il{ix, iy, iz};
    Vec3 t = rotate(b.rot.conj(), Iout); // placeholder, replaced below
    (void)t; (void)il;
}

// apply impulse j (scalar along n, mass already folded by caller as dv) at offset r.
// Here jv is the desired DELTA VELOCITY contribution: we compute angular response
// from real inertia tensor so units stay consistent.
static void applyImpulseVel(BoxProp& b, const Vec3* ax, const float he[],
                            const Vec3& r, const Vec3& imp /* J vector */) {
    float m = b.hx * b.hy * b.hz * 8.f;      // density=1 volume mass
    b.vel = b.vel + imp * (1.f / m);

    Vec3 torque = cross(r, imp);
    // I_inv in world: R * Iinv_local * R^T ; Iinv_local diag
    float hx2 = (2*b.hx)*(2*b.hx), hy2 = (2*b.hy)*(2*b.hy), hz2 = (2*b.hz)*(2*b.hz);
    float ixl = 12.f / (m * (hy2 + hz2));
    float iyl = 12.f / (m * (hx2 + hz2));
    float izl = 12.f / (m * (hx2 + hy2));
    // tau_local = R^T * torque ; dw_local = Iinv_local .* tau_local ; dw_world = R * dw_local
    Vec3 tl = rotate(b.rot.conj(), torque);
    Vec3 dwl{ tl.x * ixl, tl.y * iyl, tl.z * izl };
    b.angVel = b.angVel + rotate(b.rot, dwl);
}

// effective inverse mass of box along normal n at contact offset r
static float effInvMass(const BoxProp& b, const Vec3* ax, const Vec3& r, const Vec3& n) {
    float m = b.hx * b.hy * b.hz * 8.f;
    float hx2 = (2*b.hx)*(2*b.hx), hy2 = (2*b.hy)*(2*b.hy), hz2 = (2*b.hz)*(2*b.hz);
    float ixl = 12.f / (m * (hy2 + hz2));
    float iyl = 12.f / (m * (hx2 + hz2));
    float izl = 12.f / (m * (hx2 + hy2));
    Vec3 rxn = cross(r, n);
    Vec3 tl = rotate(b.rot.conj(), rxn);
    Vec3 dwl{ tl.x * ixl, tl.y * iyl, tl.z * izl };
    Vec3 dw = rotate(b.rot, dwl);
    return 1.f/m + dot(cross(dw, r), n);   // 1/m + n·((I⁻¹(r×n))×r)
}

void VerletWorld::wake(BoxProp& b) { b.sleeping = false; b.sleepTimer = 0.f; }

void VerletWorld::collideBoxesParticles() {
    for (auto& b : boxes_) {
        Vec3 ax[3]; boxAxes(b.rot, ax);
        float he[3] = { b.hx, b.hy, b.hz };
        for (auto& p : particles_) {
            if (p.invMass <= 0.f) continue;
            Vec3 push;
            if (!sphereOBB(p.pos, p.radius, b.pos, ax, he, push)) continue;

            Vec3 cp = p.pos - push;
            Vec3 r = cp - b.pos;
            Vec3 n = normalize(push);
            wake(b);

            Vec3 pv = (p.pos - p.prev);
            Vec3 relv = pointVel(b, r) - pv;   // relative velocity box-vs-particle
            float vn = dot(relv, n);

            p.pos = p.pos + push;
            p.prev = p.prev + push * 0.4f;

            if (vn > 0.f && !b.sleeping) {
                // particle is lighter side; push box away along -n proportionally
                float bmEff = 1.f / effInvMass(b, ax, r, -n);
                float pm = 1.f / p.invMass;
                float j = vn * (pm * bmEff) / (pm + bmEff);
                applyImpulseVel(b, ax, he, r, -n * j);
            }
        }
    }
}

Vec3 VerletWorld::collideSphereWithBox(const Vec3& posIn, float radius, int boxIdx,
                                       Vec3* velInOut) {
    BoxProp& b = boxes_[boxIdx];
    Vec3 ax[3]; boxAxes(b.rot, ax);
    float he[3] = { b.hx, b.hy, b.hz };
    Vec3 push;
    if (!sphereOBB(posIn, radius, b.pos, ax, he, push))
        return Vec3{};

    Vec3 cp = posIn - push;
    Vec3 r = cp - b.pos;
    Vec3 n = normalize(push);           // away from box, toward player
    wake(b);

    Vec3& v = *velInOut;
    Vec3 bv = pointVel(b, r);
    float vrel = dot(v - bv, n);        // negative when approaching

    if (vrel < 0.f) {
        // player (70kg) shoves the box; box gets AT MOST ~player's speed.
        float bmEff = 1.f / effInvMass(b, ax, r, n);
        float pm = 70.f;
        float j = -vrel * (pm * bmEff) / (pm + bmEff);
        applyImpulseVel(b, ax, he, r, n * j);
        // cap: box must not end up moving away FASTER than the player
        float after = dot(pointVel(b, r), n);
        float cap = -vrel;                       // |approach| speed
        if (after > cap && after > 0.f) {
            float excess = (after - cap) / std::max(1e-3f, 1.f / bmEff);
            applyImpulseVel(b, ax, he, r, n * (-excess * 0.9f));
        }
    }
    // ALWAYS hard-stop player velocity into the box surface — this is what
    // makes it solid no matter what the box does
    float bvN = dot(pointVel(b, r), n);
    float into = dot(v, n) - bvN;
    if (into < 0.f) v = v - n * into;

    // positional: never allow overlap — sphere rides on the surface
    return push;
}

void VerletWorld::collideBoxesBoxes() {
    for (size_t i = 0; i < boxes_.size(); ++i) {
        for (size_t j = i + 1; j < boxes_.size(); ++j) {
            BoxProp& A = boxes_[i];
            BoxProp& B = boxes_[j];
            if (A.sleeping && B.sleeping) continue;
            float ra = (A.hx + A.hy + A.hz) * 0.62f;
            float rb = (B.hx + B.hy + B.hz) * 0.62f;
            Vec3 d = B.pos - A.pos;
            float dist2 = dot(d, d);
            float rsum = ra + rb;
            if (dist2 > rsum * rsum || dist2 < 1e-8f) continue;
            float dist = sqrtf(dist2);
            Vec3 n = d * (1.f / dist);
            float overlap = rsum - dist;
            float ma = A.hx*A.hy*A.hz * 8.f, mb = B.hx*B.hy*B.hz * 8.f;
            float wa = mb / (ma + mb), wb = ma / (ma + mb);
            A.pos = A.pos - n * (overlap * wa);
            B.pos = B.pos + n * (overlap * wb);
            wake(A); wake(B);
            Vec3 cp = A.pos + n * ra;
            Vec3 ra_ = cp - A.pos, rb_ = cp - B.pos;
            Vec3 axA[3], axB[3];
            boxAxes(A.rot, axA); boxAxes(B.rot, axB);
            Vec3 relv = pointVel(B, rb_) - pointVel(A, ra_);
            float vn = dot(relv, n);
            if (vn < 0.f) {
                float eA = effInvMass(A, axA, ra_, n);
                float eB = effInvMass(B, axB, rb_, n);
                float j = -vn / (eA + eB);
                applyImpulseVel(A, axA, nullptr, ra_, -n * (j * eA * ma));
                applyImpulseVel(B, axB, nullptr, rb_,  n * (j * eB * mb));
            }
        }
    }
}

void VerletWorld::integrateBoxes(float dt) {
    for (auto& b : boxes_) {
        if (b.sleeping) continue;
        b.vel = b.vel + gravity_ * dt;
        b.pos = b.pos + b.vel * dt;
        Vec3 w = b.angVel * 0.5f;
        Quat dw{ w.x, w.y, w.z, 0.f };
        Quat dq = dw * b.rot;
        b.rot = Quat{ b.rot.x + dq.x*dt, b.rot.y + dq.y*dt,
                      b.rot.z + dq.z*dt, b.rot.w + dq.w*dt }.normalized();
    }
}

void VerletWorld::collideBoxesTerrain() {
    if (!hf_) return;
    for (auto& b : boxes_) {
        Vec3 ax[3]; boxAxes(b.rot, ax);
        float he[3] = { b.hx, b.hy, b.hz };

        // terrain normal under box center
        float e = 0.35f;
        float hx1 = hf_->heightAt(b.pos.x + e, b.pos.z);
        float hx0 = hf_->heightAt(b.pos.x - e, b.pos.z);
        float hz1 = hf_->heightAt(b.pos.x, b.pos.z + e);
        float hz0 = hf_->heightAt(b.pos.x, b.pos.z - e);
        Vec3 tn = normalize(Vec3{ -(hx1-hx0)/(2*e), 1.f, -(hz1-hz0)/(2*e) });

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

            Vec3 v = pointVel(b, rc);
            float vn = dot(v, tn);
            if (vn < 0.f) {
                // bounce ONLY on genuinely hard hits; soft touches get zero
                // restitution so resting bodies don't chatter
                float e_rest = (vn < -3.f) ? 0.35f : 0.f;

                float em = effInvMass(b, ax, rc, tn);
                float jn = -(1.f + e_rest) * vn / em;
                if (jn > 0.f) applyImpulseVel(b, ax, he, rc, tn * jn);

                // Coulomb friction against tangential motion
                Vec3 vt = v - tn * vn;
                float tl = length(vt);
                if (tl > 1e-3f) {
                    float et = effInvMass(b, ax, rc, normalize(-vt));
                    float jt = tl / et;
                    float maxF = 0.65f * jn;
                    if (jt > maxF) jt = maxF;
                    applyImpulseVel(b, ax, he, rc, normalize(-vt) * jt);
                }
            }
        }

        if (contacts > 0) {
            // Baumgarte-style positional lift with SLOP: only correct what
            // exceeds the slop band, and only partially per frame — kills jitter
            const float kSlop = 0.02f;
            float over = maxPen - kSlop;
            if (over > 0.f) b.pos.y += over * 0.35f;

            // sleep: slow body on ground freezes solid (no more trembling)
            if (length(b.vel) < 0.30f && length(b.angVel) < 0.40f) {
                b.sleepTimer += 1.f/60.f;
                if (b.sleepTimer > 0.35f) {
                    b.sleeping = true;
                    b.vel = Vec3{}; b.angVel = Vec3{};
                }
            } else {
                b.sleepTimer = 0.f;
            }
        } else {
            b.sleepTimer = 0.f;
        }
    }
}

// player/nudge interactions must wake a sleeping box
// (wake() is called from collideSphereWithBox / collideBoxesParticles / Boxes)

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
