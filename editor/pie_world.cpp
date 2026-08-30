#include "editor/pie_world.h"
#include <cstdio>
#include <cmath>

namespace bip {

namespace {
// Euler degrees (pitch=X, yaw=Y, roll=Z) -> quaternion, applied Y * X * Z.
static Quat quatFromDeg(const float d[3]) {
    const float k = 3.14159265f / 180.f * 0.5f;
    float cp = cosf(d[0]*k), sp = sinf(d[0]*k);
    float cy = cosf(d[1]*k), sy = sinf(d[1]*k);
    float cr = cosf(d[2]*k), sr = sinf(d[2]*k);

    // q = qY * qX * qZ
    Quat qx{sp, 0, 0, cp};
    Quat qy{0, sy, 0, cy};
    Quat qz{0, 0, sr, cr};
    return (qy * qx * qz).normalized();
}
} // namespace

void PIEWorld::applyScene(const Scene& s) {
    phys.boxes_.clear();
    for (const auto& sb : s.boxes) {
        Vec3 p{sb.pos[0], sb.pos[1], sb.pos[2]};
        int idx = phys.addBox(p, sb.half[0], sb.half[1], sb.half[2]);
        if (idx >= 0) {
            phys.boxes_[(size_t)idx].rot = quatFromDeg(sb.rotDeg);
            phys.boxes_[(size_t)idx].owner = (uint8_t)sb.owner;
        }
    }
    // route: authored holds if present, else procedural generation
    std::vector<Hold> authored;
    authored.reserve(s.holds.size());
    for (const auto& sh : s.holds)
        authored.push_back(Hold{ Vec3{sh.pos[0], sh.pos[1], sh.pos[2]}, sh.checkpoint });
    if (!authored.empty()) route.buildFromScene(authored, hf);
    else                   route.generate(hf, s.seed);
}

bool PIEWorld::start(const Scene& authored, ID3D11Device*) {
    snapshot = authored;                 // Play-time snapshot (Unreal-style)
    hf.generate(authored.seed, authored.worldSize, authored.heightN);

    phys.init(&hf);
    physBuddy.init(&hf);
    applyScene(authored);

    Vec3 spawn{ authored.spawn[0], authored.spawn[1], authored.spawn[2] };
    spawn.y = hf.heightAt(spawn.x, spawn.z) + 1.2f;
    player.init(&phys, &hf, spawn);
    run.reset(spawn);
    buddyActive = false;
    active_ = true;
    return true;
}

void PIEWorld::stop() {
    player.shutdown(&phys);
    if (buddyActive) { buddy.shutdown(&physBuddy); buddyActive = false; }
    phys.boxes_.clear();
    physBuddy.boxes_.clear();
    active_ = false;
}

// Hot-apply scene edits while playing: boxes/holds are rebuilt in place,
// but the player keeps its transform so gameplay is not interrupted.
// This is the part Unreal cannot do (it requires stopping PIE first).
void PIEWorld::rebuildFromScene(const Scene& s) {
    if (active_) {
        Vec3 keep = player.pelvisPos();
        float yaw = 0.f;
        applyScene(s);
        player.respawn(keep);
        player.setFacingYaw(yaw);
    } else {
        snapshot = s;
        applyScene(s);
    }
}

void PIEWorld::step(float dt) {
    if (!active_) return;
    player.control(Vec3{}, false, false, false, Vec3{}, Vec3{});
    phys.step(dt);
    player.simulate(dt);
    if (buddyActive) { physBuddy.step(dt); buddy.simulate(dt); }
}

} // namespace bip
