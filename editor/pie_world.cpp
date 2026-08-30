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
    gp.init(&hf);           // REQUIRED: gameplay reads terrain height for the
                            // summit check; without it tick() derefs nullptr.
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

void PIEWorld::step(float dt, const InputState* input, float camYaw) {
    if (!active_) return;

    Vec3 move{};
    bool wantJump = false, grabL = false, grabR = false;
    Vec3 grabPtL{}, grabPtR{};

    if (input) {
        float cy = camYaw;
        Vec3 f{ sinf(cy), 0.f, cosf(cy) };
        Vec3 r{ cosf(cy), 0.f, -sinf(cy) };
        if (input->down('W')) move = move + f;
        if (input->down('S')) move = move - f;
        if (input->down('A')) move = move - r;
        if (input->down('D')) move = move + r;
        wantJump = input->down(VK_SPACE);

        Vec3 pelvis = player.pelvisPos();
        grabPtL = pelvis + f * 0.9f + Vec3{-0.35f, 0.35f, 0};
        grabPtR = pelvis + f * 0.9f + Vec3{ 0.35f, 0.35f, 0};
        grabL = input->mouseButtons[0] && !run.exhausted;
        grabR = input->mouseButtons[1] && !run.exhausted;
        if (grabL || grabR) {
            int h = route.nearest(pelvis + Vec3{0, 0.4f, 0}, 2.2f);
            if (h >= 0) {
                const Hold& hold = route.holds()[h];
                if (grabL) grabPtL = hold.pos;
                if (grabR) grabPtR = hold.pos;
            }
        }
        player.setFacingYaw(camYaw);
    }

    player.control(move, wantJump, grabL, grabR, grabPtL, grabPtR);
    phys.step(dt);
    player.simulate(dt);
    if (buddyActive) { physBuddy.step(dt); buddy.simulate(dt); }

    // gameplay tick (stamina / falls), same as the game
    Vec3 p2 = player.pelvisPos();
    gp.tick(run, p2, grabL || grabR, grabL || grabR, dt);
}

} // namespace bip
