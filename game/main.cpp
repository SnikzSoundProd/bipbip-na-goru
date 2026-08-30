// bipbip — «БИП БИП НА ГОРУ»
// Phase 2.2-2.4: verlet ragdoll climber, WASD + jump + two-hand grabbing,
// chase camera. Boxes tumble around him.
#include "platform/window_win32.h"
#include "render/dx11_device.h"
#include "render/shader_manager.h"
#include "render/mesh.h"
#include "render/camera.h"
#include "render/frustum.h"
#include "world/heightfield.h"
#include "physics/verlet.h"
#include "game/player/climber.h"
#include "game/world/route.h"
#include "game/gameplay/run.h"
#include "render/text_renderer.h"
#include "net/net_layer.h"
#include "game/ui/menu_state.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "core/scene.h"
#include "core/game_config.h"
#include <ctime>
#include <windows.h>
#include <io.h>   // _access for projectRoot() asset-path walk-up

using namespace bip;

// Resolve the project root so asset paths work no matter which folder the
// user launches from. Strategy: dir of the exe, then walk up until we find
// the "assets" folder (or hit a drive root).
static std::string projectRoot() {
    wchar_t buf[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::string cur;
    if (n) {
        int wlen = WideCharToMultiByte(CP_UTF8, 0, buf, (int)n, nullptr, 0, nullptr, nullptr);
        std::string tmp(wlen, '\0');
        WideCharToMultiByte(CP_UTF8, 0, buf, (int)n, tmp.data(), wlen, nullptr, nullptr);
        size_t pos = tmp.find_last_of("/\\");
        cur = (pos != std::string::npos) ? tmp.substr(0, pos + 1) : "./";
    } else {
        cur = "./";
    }
    // walk up looking for /assets
    for (int guard = 0; guard < 8; ++guard) {
        std::string probe = cur + "assets";
        // use stat via _access to test existence
        if (_access(probe.c_str(), 0) == 0) return cur;
        size_t sl = cur.find_last_of("/\\", cur.size() - 2);
        if (sl == std::string::npos) break;
        cur = cur.substr(0, sl + 1);
    }
    return cur;  // fallback: whatever we have
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    fprintf(stderr, "[boot] starting\n");

    uint64_t seed = (argc > 1) ? strtoull(argv[1], nullptr, 10) : 1337ull;
    std::string assetRoot = projectRoot();   // walks up to the folder containing /assets
    fprintf(stderr, "[boot] assetRoot=%s\n", assetRoot.c_str());

    // Tunables live in one place so the editor can expose them (and so the
    // game and editor cannot drift apart). Missing file = built-in defaults,
    // which are then written out so the user has something to edit.
    GameConfig cfg;
    cfg.reset();
    if (!cfg.load(assetRoot + "assets/config/game.cfg")) {
        // ensure the folder exists, then write defaults so there is a file to edit
        CreateDirectoryA((assetRoot + "assets").c_str(), nullptr);
        CreateDirectoryA((assetRoot + "assets/config").c_str(), nullptr);
        cfg.save(assetRoot + "assets/config/game.cfg");
        fprintf(stderr, "[boot] wrote default config\n");
    }
    seed = cfg.world.seed;   // config wins, CLI arg kept for quick overrides
    if (argc > 1) seed = strtoull(argv[1], nullptr, 10);

    // net modes: bipbip [seed] --host | --join IP
    enum class NetMode { Solo, Host, Join };
    NetMode netMode = NetMode::Solo;
    std::string joinIp = "127.0.0.1";
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--host")) netMode = NetMode::Host;
        else if (!strcmp(argv[i], "--join") && i + 1 < argc) { netMode = NetMode::Join; joinIp = argv[++i]; }
    }
    bool isSolo = (netMode == NetMode::Solo);

    Window window;
    fprintf(stderr, "[boot] creating window\n");
    if (!window.create("BIP BIP NA GORU", 1280, 720)) { fprintf(stderr, "window failed\n"); return 1; }
    Dx11Device gfx;
    fprintf(stderr, "[boot] init d3d11\n");
    if (!gfx.init(window.handle(), 1280, 720)) { fprintf(stderr, "d3d11 failed\n"); return 2; }

    ShaderManager shaders;
    if (!shaders.init(gfx.device())) return 3;
    fprintf(stderr, "[boot] loading shaders from %s\n", (assetRoot + "assets/shaders/basic_vs.hlsl").c_str());

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex,pos),    D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex,normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(Vertex,uv),      D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex,color),   D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    ID3D11VertexShader* vs = nullptr; ID3D11InputLayout* il = nullptr; ID3D11PixelShader* ps = nullptr;
    std::string errs;
    if (!shaders.loadVertexShader(assetRoot + "assets/shaders/basic_vs.hlsl", "main", layout, 4, &vs, &il, &errs)) {
        fprintf(stderr, "VS error:\n%s\n", errs.c_str()); return 4;
    }
    if (!shaders.loadPixelShader(assetRoot + "assets/shaders/basic_ps.hlsl", "main", &ps, &errs)) {
        fprintf(stderr, "PS error:\n%s\n", errs.c_str()); return 5;
    }

    // --- world ---------------------------------------------------------------
    HeightField hf;
    hf.generate(seed, cfg.world.worldSize, cfg.world.heightN);
    Mesh mountain;
    {
        const auto& vv = hf.vertices();
        const auto& ii = hf.indices();
        if (!mountain.init(gfx.device(), vv.data(), (uint32_t)vv.size(),
                           ii.data(), (uint32_t)ii.size())) return 6;
    }

    VerletWorld phys;
    phys.init(&hf);

    // Separate VerletWorld for the remote player (puppet) so its particles
    // never collide/share indices with the local climber.
    VerletWorld physBuddy;
    physBuddy.init(&hf);

    // crate mesh (tinted at draw time)
    auto boxVerts = geom::box(1.f, 1.f, 1.f);
    auto boxIdx = geom::boxIndices();
    Mesh unitMesh;
    if (!unitMesh.init(gfx.device(), boxVerts.data(), (uint32_t)boxVerts.size(),
                       boxIdx.data(), (uint32_t)boxIdx.size())) return 8;

    for (int i = 0; i < 14; ++i) {
        Rng rng(seed ^ (0xC0FFEE + i));
        float a = rng.unit() * 6.2831853f;
        float r = 20.f + rng.unit() * 60.f;
        float x = cosf(a) * r, z = sinf(a) * r;
        float y = hf.heightAt(x, z) + 2.f + rng.unit() * 15.f;
        float s = 1.2f + rng.unit() * 1.2f;
        phys.addBox(Vec3{x, y, z}, s*0.5f, s*0.5f, s*0.5f);
    }

    // the boi
    Climber player;
    // Both peers start the client avatar at the same deterministic spawn.
    // Host later confirms this position through reconciliation.
    const Vec3 clientSpawn{2.f, hf.heightAt(2.f, -78.f), -78.f};
    player.init(&phys, &hf, netMode == NetMode::Join ? clientSpawn
                                                       : Vec3{0, 0, -80});
    Climber::PartBox parts[32];

    // --- gameplay: route + run state
    Route route;
    route.generate(hf, seed);
    RunState run;
    run.reset(Vec3{0, hf.heightAt(0.f, 118.f) , 118.f});
    Gameplay gp;
    gp.init(&hf);

    // hold meshes: small orange cube for holds, big one for checkpoints
    Mesh holdMesh;
    {
        auto hv = geom::boxColored(1.f,1.f,1.f, 0.95f, 0.55f, 0.15f);
        auto hi = geom::boxIndices();
        if (!holdMesh.init(gfx.device(), hv.data(), (uint32_t)hv.size(),
                           hi.data(), (uint32_t)hi.size())) return 9;
    }

    TextRenderer hud;
    if (!hud.init(gfx.device(), 1280, 720)) return 10;

    // --- net + second player
    NetLayer net;
    // No CLI mode means a real menu: do not bind/connect until user confirms.
    const bool cliLaunch = argc > 1;
    ConnectionMenu menu;
    bool showMenu = !cliLaunch;
    if (!showMenu) {
        if (netMode == NetMode::Host && !net.host(cfg.network.port)) return 11;
        if (netMode == NetMode::Join && !net.join(joinIp, cfg.network.port)) return 11;
    }

    Climber buddy;   // remote player: host simulates from client input,
                     // client renders interpolated snapshots
    bool buddyActive = false;
    double netTimer = 0.0;
    uint32_t inputSeq = 0;
    const double kNetRate = cfg.network.sendRate;
    FILE* buddbg = nullptr;   // temp net-debug file handle (flush + close after use)

    // constant buffers
    struct CBPerFrame { float viewProj[16]; };
    struct CBPerObject { float world[16]; };
    struct CBPerTint  { float tint[4]; };
    ID3D11Buffer* cbFrame = nullptr; ID3D11Buffer* cbObj = nullptr; ID3D11Buffer* cbTint = nullptr;
    D3D11_BUFFER_DESC cbd{}; cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.ByteWidth = sizeof(CBPerFrame); cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    gfx.device()->CreateBuffer(&cbd, nullptr, &cbFrame);
    cbd.ByteWidth = sizeof(CBPerObject);
    gfx.device()->CreateBuffer(&cbd, nullptr, &cbObj);
    cbd.ByteWidth = sizeof(CBPerTint);
    gfx.device()->CreateBuffer(&cbd, nullptr, &cbTint);

    float identity[16] = { 1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1 };
    float whiteTint[4] = { 1,1,1,1 };

    InputState input;
    uint64_t frame = 0;
    double simTime = 0;
    const double kFixedDt = cfg.physics.fixedDt;
    double acc = 0;
    Camera chaseCam;                       // yaw/pitch driven by mouse, pos by player
    chaseCam.pos = Vec3{0, 5, -90};

    Frustum culler;                        // frustum cull for dynamic objects
    float frustumPlanes[6][4] = {};
    int drawCalls = 0;                      // incremented per actual draw
    bool showProfiler = false;             // F3 toggles
    int frameCount = 0;
    double fpsAccum = 0, fpsTime = 0;
    float fps = 0, frameMs = 0;
    double tFrameStart = (double)clock() / CLOCKS_PER_SEC;

    bool prevEnter = false, prevUp = false, prevDown = false, prevBack = false;
    while (!window.shouldClose()) {
        window.pumpMessages(input);
        bool enterPressed = input.down(VK_RETURN) && !prevEnter;
        bool upPressed = input.down(VK_UP) && !prevUp;
        bool downPressed = input.down(VK_DOWN) && !prevDown;
        bool backPressed = input.down(VK_BACK) && !prevBack;
        if (showMenu) {
            if (upPressed) menu.selected = (menu.selected + 3) % 4;
            if (downPressed) menu.selected = (menu.selected + 1) % 4;
            if (menu.state == MenuState::Join) {
                if (backPressed) menu.eraseChar();
                for (uint8_t ti = 0; ti < input.textCount; ++ti) menu.appendChar(input.text[ti]);
            }
            if (input.down(VK_ESCAPE) && menu.state != MenuState::Main) menu.back();
            if (enterPressed) {
                StartMode selectedMode{};
                if (menu.activate(&selectedMode)) {
                    showMenu = false;
                    if (selectedMode == StartMode::Solo) { netMode = NetMode::Solo; isSolo = true; }
                    else if (selectedMode == StartMode::Host) {
                        netMode = NetMode::Host; isSolo = false;
                        if (!net.host(cfg.network.port)) {
                            menu.error = "HOST FAILED: PORT 27015 IS BUSY";
                            menu.state = MenuState::Error; showMenu = true;
                        }
                    } else {
                        netMode = NetMode::Join; isSolo = false; joinIp = menu.ip;
                        if (!net.join(joinIp, cfg.network.port)) {
                            menu.error = "JOIN FAILED: CONNECTION NOT STARTED";
                            menu.state = MenuState::Error; showMenu = true;
                        }
                    }
                }
            }
            if (menu.state == MenuState::Quit) break;
        }
        prevEnter = input.down(VK_RETURN); prevUp = input.down(VK_UP);
        prevDown = input.down(VK_DOWN); prevBack = input.down(VK_BACK);

        // F3 toggles the profiler overlay (works in menu and in-game)
        static bool prevF3 = false;
        bool f3 = input.down(VK_F3);
        if (f3 && !prevF3) showProfiler = !showProfiler;
        prevF3 = f3;
        if (input.down(VK_ESCAPE) && !showMenu) break;

        RECT rc; GetClientRect(window.handle(), &rc);
        if (rc.right > 0 && rc.bottom > 0) gfx.resize(rc.right, rc.bottom);
        float aspect = (float)rc.right / (float)rc.bottom;

        acc += 1.0 / 60.0;
        int steps = 0;
        while (acc >= kFixedDt && steps < 4) {
            if (showMenu) { acc = 0.0; break; } // menu pauses simulation
            // movement basis from camera yaw
            float cy = chaseCam.yaw;
            Vec3 f{ sinf(cy), 0, cosf(cy) };
            Vec3 r{ cosf(cy), 0, -sinf(cy) };
            Vec3 move{};
            if (input.down('W')) move = move + f;
            if (input.down('S')) move = move - f;
            if (input.down('A')) move = move - r;
            if (input.down('D')) move = move + r;
            bool wantJump = input.down(VK_SPACE);

            // grab targets: nearest route hold in front, else chest-height probe
            Vec3 pelvis = player.pelvisPos();
            Vec3 lookF{ sinf(chaseCam.yaw), 0.f, cosf(chaseCam.yaw) };
            Vec3 grabPtL = pelvis + lookF * 0.9f + Vec3{-0.35f, 0.35f, 0};
            Vec3 grabPtR = pelvis + lookF * 0.9f + Vec3{ 0.35f, 0.35f, 0};
            bool grabL = input.mouseButtons[0] && !run.exhausted;
            bool grabR = input.mouseButtons[1] && !run.exhausted;
            if (grabL || grabR) {
                int h = route.nearest(pelvis + Vec3{0, 0.4f, 0}, cfg.gameplay.grabReach);
                if (h >= 0) {
                    const Hold& hold = route.holds()[h];
                    Vec3 d = hold.pos - pelvis;
                    if (fabsf(d.x) > 0.05f) {
                        if (grabL) grabPtL = hold.pos;
                        if (grabR) grabPtR = hold.pos;
                    }
                }
            }

            // Camera determines where the local avatar faces, even while idle.
            player.setFacingYaw(chaseCam.yaw);
            player.control(move, wantJump, grabL, grabR, grabPtL, grabPtR);

            phys.step((float)kFixedDt);
            player.simulate((float)kFixedDt);
            if (buddyActive && netMode == NetMode::Host) {
                // Buddy has its own particle pool and therefore needs its own
                // Verlet step. Without this, the pelvis moves while limbs stay
                // at the spawn pose and render as giant stretched beams.
                physBuddy.step((float)kFixedDt);
                buddy.simulate((float)kFixedDt);
            }

            // Shared-box ownership: the last player whose pelvis touches a box
            // becomes its logical owner. The host still performs the single
            // authoritative simulation; ownership prevents ambiguous handoff.
            if (netMode == NetMode::Host) {
                auto touchBox = [](const Vec3& p, const BoxProp& b) {
                    float reach = std::max(b.hx, std::max(b.hy, b.hz)) + 0.65f;
                    return length(p - b.pos) <= reach;
                };
                for (auto& b : phys.boxes_) {
                    if (touchBox(player.pelvisPos(), b)) b.owner = 0;
                    if (buddyActive && touchBox(buddy.pelvisPos(), b)) b.owner = 1;
                }
            }

            // gameplay tick
            Vec3 p2 = player.pelvisPos();
            bool hanging = grabL || grabR;
            gp.tick(run, p2, hanging, hanging, kFixedDt);

            // checkpoint pickup: nearest checkpoint hold within 1.5m
            int ch = route.nearest(p2, 1.6f);
            if (ch >= 0 && route.holds()[ch].checkpoint && ch != run.lastCheckpoint) {
                run.lastCheckpoint = ch;
                run.respawn = route.holds()[ch].pos + Vec3{0, 1.f, 0};
            }

            // fall detection: way below terrain near route => fell off a cliff
            float gy = hf.heightAt(p2.x, p2.z);
            static Vec3 lastSafe = run.respawn;
            static float lastSafeY = run.respawn.y;
            if (player.grounded() && p2.y > gy - 0.5f) { lastSafe = p2; lastSafeY = p2.y; }
            if (p2.y < lastSafeY - cfg.gameplay.fallDistance) {   // fell below last safe spot
                Vec3 rp;
                gp.onFall(run, &rp);
                player.respawn(rp);
                lastSafe = rp; lastSafeY = rp.y;
                phys.boxes_.clear();                 // (props stay put; particles reset below)
            }

            acc -= kFixedDt; simTime += kFixedDt; ++steps;

            // ---- networking (20Hz)
            if (!isSolo) {
                net.pump((float)kFixedDt);
                netTimer += kFixedDt;
                if (netTimer >= kNetRate) {
                    netTimer -= kNetRate;

                    if (netMode == NetMode::Join && net.connected()) {
                        // client: send MY input, host simulates it
                        net::InputPacket ip;
                        ip.seq = ++inputSeq;
                        Vec3 mv = move;  // last computed
                        ip.moveX = mv.x; ip.moveZ = mv.z;
                        ip.buttons = (wantJump ? net::InputPacket::BTN_JUMP : 0)
                                   | (input.mouseButtons[0] ? net::InputPacket::BTN_GRABL : 0)
                                   | (input.mouseButtons[1] ? net::InputPacket::BTN_GRABR : 0);
                        ip.camYaw = chaseCam.yaw;
                        net.sendInput(ip);
                    }
                    if (netMode == NetMode::Host) {
                        // host: send MY pelvis to client
                        Vec3 mp = player.pelvisPos();
                        net::PlayerSnapshot snap;
                        snap.lastSeq++;
                        snap.px = mp.x; snap.py = mp.y; snap.pz = mp.z;
                        snap.vyaw = chaseCam.yaw;
                        snap.stamina = run.stamina;
                        snap.flags = 0;
                        player.writePose(snap.pose);
                        if (buddyActive) {
                            Vec3 cp = buddy.pelvisPos();
                            snap.clientPx = cp.x; snap.clientPy = cp.y; snap.clientPz = cp.z;
                            snap.clientYaw = net.remoteInput.camYaw;
                            snap.clientAckSeq = net.remoteInput.seq;
                        } else {
                            snap.clientPx = snap.clientPy = snap.clientPz = 0.f;
                            snap.clientYaw = 0.f; snap.clientAckSeq = 0;
                        }
                        for (int bi = 0; bi < 14; ++bi) {
                            auto& b = snap.boxes[bi];
                            if (bi < (int)phys.boxes_.size()) {
                                const auto& src = phys.boxes_[bi];
                                b.px = src.pos.x; b.py = src.pos.y; b.pz = src.pos.z;
                                b.qx = src.rot.x; b.qy = src.rot.y;
                                b.qz = src.rot.z; b.qw = src.rot.w;
                                b.owner = src.owner;
                            } else {
                                b = {};
                            }
                        }
                        net.sendSnapshot(snap);
                    }

                    // host: apply remote input to buddy climber
                    if (netMode == NetMode::Host && net.haveRemoteInput && !buddyActive) {
                        // spawn the client puppet AT the client's real reported position
                        Vec3 cp{2.f, hf.heightAt(2.f, -78.f), -78.f};
                        // The client has not sent a transform; spawn both peers
                        // from the deterministic shared start and reconcile later.
                        buddbg = fopen("C:/Users/apex/AppData/Local/Temp/debug_net.txt", "a");
                        if (buddbg) { fprintf(buddbg, "[host] spawn buddy at client pos %.1f %.1f %.1f\n", cp.x, cp.y, cp.z); fclose(buddbg); }
                        buddy.init(&physBuddy, &hf, cp);
                        buddyActive = true;
                        fprintf(stderr, "[net] buddy spawned\n");
                    }
                    if (netMode == NetMode::Host && buddyActive && net.haveRemoteInput) {
                        net::InputPacket& ri = net.remoteInput;
                        buddy.setFacingYaw(ri.camYaw);
                        Vec3 rmv{ ri.moveX, 0, ri.moveZ };
                        bool rj = ri.buttons & net::InputPacket::BTN_JUMP;
                        bool rl = ri.buttons & net::InputPacket::BTN_GRABL;
                        bool rr = ri.buttons & net::InputPacket::BTN_GRABR;
                        Vec3 bp = buddy.pelvisPos();
                        Vec3 bf{ sinf(ri.camYaw), 0.f, cosf(ri.camYaw) };
                        Vec3 gL = bp + bf * 0.9f + Vec3{-0.35f, 0.35f, 0};
                        Vec3 gR = bp + bf * 0.9f + Vec3{ 0.35f, 0.35f, 0};
                        int hIdx = route.nearest(bp + Vec3{0, 0.4f, 0}, 2.2f);
                        if (hIdx >= 0 && (rl || rr)) {
                            const Hold& hold = route.holds()[hIdx];
                            if (rl) gL = hold.pos;
                            if (rr) gR = hold.pos;
                        }
                        buddy.control(rmv, rj, rl, rr, gL, gR);
                    }

                    // Client sends input only. Player position is authoritative on host
                    // and comes back in PlayerSnapshot below for reconciliation.

                    // client: apply received snapshot to buddy puppet directly
                    if (netMode == NetMode::Join && net.haveRemoteSnapshot) {
                        if (!buddyActive) {
                            Vec3 sp{net.remoteSnapshot.px, net.remoteSnapshot.py, net.remoteSnapshot.pz};
                            buddbg = fopen("C:/Users/apex/AppData/Local/Temp/debug_net.txt", "a");
                            if (buddbg) { fprintf(buddbg, "[join] spawn buddy at host pos %.1f %.1f %.1f\n", sp.x, sp.y, sp.z); fclose(buddbg); }
                            buddy.init(&physBuddy, &hf, sp);
                            buddyActive = true;
                            fprintf(stderr, "[net] CLIENT buddy spawned\n");
                        }
                        auto& s = net.remoteSnapshot;
                        // Local prediction + reconciliation: keep responsiveness, but
                        // remove accumulated drift against the host's buddy state.
                        if (s.clientAckSeq != 0) {
                            Vec3 auth{s.clientPx, s.clientPy, s.clientPz};
                            Vec3 err = auth - player.pelvisPos();
                            if (length(err) > 0.05f)
                                player.reconcilePelvis(auth, length(err) > 2.0f ? 1.0f : 0.18f);
                        }
                        buddy.applyPose(s.pose);
                        // Pose is already physically rotated on the host; do not
                        // rotate this puppet a second time.
                        buddy.setRenderFacingYaw(s.vyaw);
                        // Shared crates are host-authoritative: clients only
                        // consume transforms and never run a second simulation.
                        const int boxCount = std::min(14, (int)phys.boxes_.size());
                        for (int bi = 0; bi < boxCount; ++bi) {
                            auto& dst = phys.boxes_[bi];
                            const auto& src = s.boxes[bi];
                            dst.pos = Vec3{src.px, src.py, src.pz};
                            dst.rot = Quat{src.qx, src.qy, src.qz, src.qw}.normalized();
                            dst.vel = Vec3{};
                            dst.angVel = Vec3{};
                            dst.owner = src.owner;
                            dst.sleeping = true;
                        }
                    }
                    // client: also feed remote HOST input? no — host is authoritative for itself.
                }
            }
        }

        // ---- chase camera
        Vec3 target = player.pelvisPos();
        if (!showMenu) {
            chaseCam.yaw += input.mouseDX * cfg.camera.sensitivity;
            chaseCam.pitch = std::max(cfg.camera.pitchMin,
                                      std::min(cfg.camera.pitchMax,
                                               chaseCam.pitch + input.mouseDY * cfg.camera.sensitivity));
        }
        Vec3 back{ -sinf(chaseCam.yaw)*cosf(chaseCam.pitch), sinf(chaseCam.pitch),
                    -cosf(chaseCam.yaw)*cosf(chaseCam.pitch) };
        Vec3 camWant = target + back * cfg.camera.distance + Vec3{0, cfg.camera.height, 0};
        float groundClear = hf.heightAt(camWant.x, camWant.z) + 0.8f;
        if (camWant.y < groundClear) camWant.y = groundClear;
        chaseCam.pos = camWant;

        float vp[16];
        chaseCam.viewProj(vp, aspect);
        Camera::frustumPlanes(vp, frustumPlanes);
        culler.setPlanes(frustumPlanes);
        drawCalls = 0;

        ID3D11DeviceContext* ctx = gfx.ctx();
        ctx->UpdateSubresource(cbFrame, 0, nullptr, vp, 0, 0);

        gfx.beginFrame(0.45f, 0.65f, 0.90f);
        ctx->IASetInputLayout(il);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->PSSetShader(ps, nullptr, 0);
        ID3D11Buffer* cbsAll[3] = { cbFrame, cbObj, cbTint };
        ctx->VSSetConstantBuffers(0, 3, cbsAll);

        ctx->UpdateSubresource(cbObj, 0, nullptr, identity, 0, 0);
        ctx->UpdateSubresource(cbTint, 0, nullptr, whiteTint, 0, 0);
        mountain.draw(ctx);
        ++drawCalls;

        // crates: brownish, full quaternion orientation
        for (auto& b : phys.boxes_) {
            float rad = std::sqrt(b.hx*b.hx + b.hy*b.hy + b.hz*b.hz) * 2.f;
            if (!culler.sphereVisible(b.pos.x, b.pos.y, b.pos.z, rad)) continue;
            Vec3 ax0 = rotate(b.rot, Vec3{1,0,0});
            Vec3 ax1 = rotate(b.rot, Vec3{0,1,0});
            Vec3 ax2 = rotate(b.rot, Vec3{0,0,1});
            float sx = b.hx*2.f, sy = b.hy*2.f, sz = b.hz*2.f;
            float w[16] = {
                ax0.x*sx, ax0.y*sx, ax0.z*sx, 0,
                ax1.x*sy, ax1.y*sy, ax1.z*sy, 0,
                ax2.x*sz, ax2.y*sz, ax2.z*sz, 0,
                b.pos.x, b.pos.y, b.pos.z, 1
            };
            ctx->UpdateSubresource(cbObj, 0, nullptr, w, 0, 0);
            float brown[4] = { 0.72f, 0.55f, 0.34f, 1 };
            ctx->UpdateSubresource(cbTint, 0, nullptr, brown, 0, 0);
            unitMesh.draw(ctx);
            ++drawCalls;
        }
        // player body parts: oriented boxes (bone-aligned, joint cubes seal gaps)
        {
            Vec3 pp = player.pelvisPos();
            if (culler.sphereVisible(pp.x, pp.y, pp.z, 3.0f)) {
            int np = player.collectParts(parts);
            for (int i = 0; i < np; ++i) {
                const auto& pb = parts[i];
                Vec3 y = pb.yAxis;
                Vec3 x = normalize(cross(y, pb.zHint));
                Vec3 z = cross(x, y);
                float sx = pb.half.x * 2.f, sy = pb.half.y * 2.f, sz = pb.half.z * 2.f;
                float w[16] = {
                    x.x*sx, x.y*sx, x.z*sx, 0,
                    y.x*sy, y.y*sy, y.z*sy, 0,
                    z.x*sz, z.y*sz, z.z*sz, 0,
                    pb.center.x, pb.center.y, pb.center.z, 1
                };
                ctx->UpdateSubresource(cbObj, 0, nullptr, w, 0, 0);
                float col[4] = { pb.color.x, pb.color.y, pb.color.z, 1 };
                ctx->UpdateSubresource(cbTint, 0, nullptr, col, 0, 0);
                unitMesh.draw(ctx);
                ++drawCalls;
            }
            }
        }
        // route holds: orange cubes (bigger for checkpoints)
        for (size_t i = 0; i < route.holds().size(); ++i) {
            const Hold& h = route.holds()[i];
            if (!culler.sphereVisible(h.pos.x, h.pos.y, h.pos.z, 1.0f)) continue;
            float s = h.checkpoint ? 0.5f : 0.22f;
            bool active = h.checkpoint && (int)i == run.lastCheckpoint;
            float tint[4] = { active ? 0.2f : 0.95f,
                              active ? 0.9f : (h.checkpoint ? 0.75f : 0.55f),
                              active ? 0.3f : (h.checkpoint ? 0.10f : 0.15f), 1.f };
            float w[16] = { s,0,0,0, 0,s,0,0, 0,0,s,0,
                            h.pos.x, h.pos.y, h.pos.z, 1 };
            ctx->UpdateSubresource(cbObj, 0, nullptr, w, 0, 0);
            ctx->UpdateSubresource(cbTint, 0, nullptr, tint, 0, 0);
            holdMesh.draw(ctx);
            ++drawCalls;
        }

        // HUD: stamina bar + timer + falls + win screen
        // remote climber (different shirt so you don't confuse yourselves ахахах)
        if (buddyActive) {
            Vec3 bp = buddy.pelvisPos();
            if (culler.sphereVisible(bp.x, bp.y, bp.z, 3.0f)) {
            int bn = buddy.collectParts(parts);
            for (int i = 0; i < bn; ++i) {
                // recolor: swap shirt->teal
                Vec3 c = parts[i].color;
                bool isShirt = fabsf(c.x - 0.85f) < 0.02f && fabsf(c.y - 0.25f) < 0.03f;
                Vec3 col = isShirt ? Vec3{0.2f, 0.75f, 0.7f} : c;
                const auto& pb = parts[i];
                Vec3 y = pb.yAxis;
                Vec3 x = normalize(cross(y, pb.zHint));
                Vec3 z = cross(x, y);
                float sx = pb.half.x * 2.f, sy = pb.half.y * 2.f, sz = pb.half.z * 2.f;
                float w[16] = {
                    x.x*sx, x.y*sx, x.z*sx, 0,
                    y.x*sy, y.y*sy, y.z*sy, 0,
                    z.x*sz, z.y*sz, z.z*sz, 0,
                    pb.center.x, pb.center.y, pb.center.z, 1
                };
                ctx->UpdateSubresource(cbObj, 0, nullptr, w, 0, 0);
                float colr[4] = { col.x, col.y, col.z, 1 };
                ctx->UpdateSubresource(cbTint, 0, nullptr, colr, 0, 0);
                unitMesh.draw(ctx);
                ++drawCalls;
            }
            }
        }

        gfx.beginUI();
        RECT rc2; GetClientRect(window.handle(), &rc2);
        hud.resize(rc2.right, rc2.bottom);

        if (showMenu) {
            const float x = 390.f, y = 170.f;
            hud.draw("BIP BIP NA GORU", x, y, 5.f, 1.f, 0.82f, 0.15f);
            hud.draw("UP/DOWN: SELECT   ENTER: CONFIRM", x, y + 70.f, 2.f, 0.82f, 0.86f, 0.95f);
            const char* items[4] = { "SOLO", "HOST GAME", "JOIN BY IP", "QUIT" };
            for (int mi = 0; mi < 4; ++mi) {
                std::string line = (menu.selected == mi ? "> " : "  ") + std::string(items[mi]);
                hud.draw(line, x, y + 110.f + mi * 32.f, 3.f,
                         menu.selected == mi ? 0.25f : 0.85f,
                         menu.selected == mi ? 1.f : 0.85f,
                         menu.selected == mi ? 0.45f : 0.9f);
            }
            if (menu.state == MenuState::Join || menu.state == MenuState::Error) {
                hud.draw("IP: " + menu.ip + "_", x, y + 255.f, 3.f, 1.f, 1.f, 1.f);
                hud.draw("TYPE IPv4, BACKSPACE EDIT, ENTER JOIN, ESC BACK", x, y + 288.f, 2.f, 0.75f, 0.8f, 0.9f);
            }
            if (menu.state == MenuState::Error)
                hud.draw(menu.error, x, y + 325.f, 2.f, 1.f, 0.25f, 0.25f);
        } else {
        // stamina bar top-left
        hud.draw("STAMINA", 16.f, 14.f, 2.f, 1, 1, 1);
        {
            int filled = (int)(run.stamina / 100.f * cfg.gameplay.staminaSegs + 0.5f);
            std::string bar;
            for (int i = 0; i < cfg.gameplay.staminaSegs; ++i) bar += (i < filled) ? '#' : '.';
            float cr = run.stamina > 40 ? 0.2f : 1.0f;
            float cg = run.stamina > 40 ? 0.85f : 0.25f;
            hud.draw(bar, 16.f, 32.f, 2.f, cr, cg, 0.2f);
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "TIME %.1f  FALLS %d", run.runTime, run.falls);
        hud.draw(buf, 16.f, 54.f, 2.f, 1, 1, 1);
        snprintf(buf, sizeof(buf), "HOLDS NEAR: %s   SEED %llu",
                 route.nearest(player.pelvisPos(), 2.5f) >= 0 ? "GRAB!" : "-",
                 (unsigned long long)seed);
        hud.draw(buf, 16.f, 74.f, 2.f, 0.85f, 0.85f, 0.9f);
        // net status line
        const char* netStatus = isSolo ? "SOLO" :
            (net.connected() ? (netMode == NetMode::Host ? "HOST: peer connected" : "JOINED") :
             (netMode == NetMode::Host ? "HOST: waiting on :27015" : "JOINING..."));
        hud.draw(netStatus, 16.f, 94.f, 2.f, net.connected() || isSolo ? 0.4f : 1.f,
                 isSolo ? 0.7f : (net.connected() ? 1.f : 0.4f), 0.4f);
        // buddy indicator (debug): shows if remote player is rendered
        if (!isSolo) {
            snprintf(buf, sizeof(buf), "BUDDY: %s", buddyActive ? "ON" : "off");
            hud.draw(buf, 16.f, 114.f, 2.f, buddyActive ? 0.3f : 0.7f,
                     buddyActive ? 1.f : 0.3f, 0.4f);
        }
        if (run.exhausted)
            hud.draw("HANDS SLIP! REST!", 480.f, 60.f, 3.f, 1, 0.25f, 0.2f);
        if (run.finished) {
            hud.draw("SUMMIT!!!", 470.f, 240.f, 8.f, 1, 0.85f, 0.1f);
            snprintf(buf, sizeof(buf), "TIME %.1fs   FALLS %d", run.finishTime, run.falls);
            hud.draw(buf, 500.f, 330.f, 3.f, 1, 1, 1);
            hud.draw("ESC TO QUIT - R FOR NEW RUN", 430.f, 380.f, 2.f, 0.9f, 0.9f, 0.9f);
            if (input.down('R')) {
                // new run: reset state (same mountain; new seed = relaunch with arg)
                run.reset(Vec3{0, hf.heightAt(0.f, 118.f), 118.f});
                player.respawn(run.respawn);
            }
        }
        } // !showMenu: gameplay HUD

        // ---- profiler overlay (F3) ----
        if (showProfiler) {
            char pb[256];
            snprintf(pb, sizeof(pb),
                "FPS %.0f   FRAME %.2f ms   DRAW CALLS %d   TRIS ~%d   SLEEP %d/%d",
                fps, frameMs, drawCalls, drawCalls * 36,
                phys.sleepingBoxCount(), (int)phys.boxes_.size());
            hud.draw(pb, 16.f, rc2.bottom - 90.f, 2.f, 0.6f, 1.f, 0.6f);
            // net graph
            char nb[256];
            snprintf(nb, sizeof(nb),
                "NET  sent %u/s  recv %u/s   (%llu sent / %llu recv)",
                net.sentPerSec, net.recvPerSec,
                (unsigned long long)net.packetsSent, (unsigned long long)net.packetsRecv);
            hud.draw(nb, 16.f, rc2.bottom - 70.f, 2.f, 0.6f, 0.8f, 1.f);
            // simple bar: sent (green) vs recv (cyan)
            int bx = 16, by = (int)(rc2.bottom - 50.f), bw = 200, bh = 8;
            float sFrac = std::min(1.f, net.sentPerSec / 60.f);
            float rFrac = std::min(1.f, net.recvPerSec / 60.f);
            for (int i = 0; i < bw; ++i) {
                if (i < (int)(sFrac * bw))
                    hud.draw("|", (float)(bx + i), (float)by, 2.f, 0.3f, 1.f, 0.4f);
                if (i < (int)(rFrac * bw))
                    hud.draw("|", (float)(bx + i), (float)(by + bh), 2.f, 0.3f, 0.9f, 1.f);
            }
            hud.draw("F3: HIDE", 16.f, rc2.bottom - 30.f, 1.5f, 0.7f, 0.7f, 0.7f);
        }

        gfx.endFrame();

        // frame timing for profiler (measured around the whole frame work)
        {
            double tEnd = (double)clock() / CLOCKS_PER_SEC;
            double dtFrame = tEnd - tFrameStart;
            tFrameStart = tEnd;
            frameMs = (float)(dtFrame * 1000.0);
            ++frameCount; fpsAccum += dtFrame;
            if (fpsAccum >= 0.5) { fps = (float)(frameCount / fpsAccum); frameCount = 0; fpsAccum = 0; }
        }

        input.endFrame();
        ++frame;
    }

    player.shutdown(&phys);
    if (buddyActive) buddy.shutdown(&phys);
    net.shutdown();
    unitMesh.shutdown(); mountain.shutdown(); gfx.shutdown(); holdMesh.shutdown();
    printf("clean exit after %llu frames\n", (unsigned long long)frame);
    return 0;
}
