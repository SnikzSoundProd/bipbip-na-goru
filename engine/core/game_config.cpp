#include "core/game_config.h"
#include <cstdio>
#include <cstring>

namespace bip {

// ---- line parsers --------------------------------------------------------
// Minimal key = value parsers. Unknown keys are skipped rather than rejected so
// a config written by a newer/older build still loads.
// "%g" prints only 6 significant digits, so fixedDt 1/60 came back as
// 0.0166667 — the game then ran at 60.0002 Hz instead of 60, and the same
// truncation hit sendRate and every tuned float. %.17g round-trips a double
// exactly, %.9g round-trips a float.
#define BIP_FMT_F "%.9g"
#define BIP_FMT_D "%.17g"

static bool parseLine(const char* line, const char* key, float& out) {
    size_t klen = strlen(key);
    if (strncmp(line, key, klen) != 0) return false;
    const char* p = line + klen;
    while (*p == ' ' || *p == '=' || *p == '\t') ++p;
    return sscanf(p, "%f", &out) == 1;
}
static bool parseLineD(const char* line, const char* key, double& out) {
    size_t klen = strlen(key);
    if (strncmp(line, key, klen) != 0) return false;
    const char* p = line + klen;
    while (*p == ' ' || *p == '=' || *p == '\t') ++p;
    return sscanf(p, "%lf", &out) == 1;
}
static bool parseLineI(const char* line, const char* key, int& out) {
    size_t klen = strlen(key);
    if (strncmp(line, key, klen) != 0) return false;
    const char* p = line + klen;
    while (*p == ' ' || *p == '=' || *p == '\t') ++p;
    return sscanf(p, "%d", &out) == 1;
}
static bool parseLineU64(const char* line, const char* key, uint64_t& out) {
    size_t klen = strlen(key);
    if (strncmp(line, key, klen) != 0) return false;
    const char* p = line + klen;
    while (*p == ' ' || *p == '=' || *p == '\t') ++p;
    return sscanf(p, "%llu", (unsigned long long*)&out) == 1;
}

// Reads engine-wide keys off one line. Shared by EngineConfig::load and
// GameConfig::load so the two can never disagree on what an "engine key" is.
static void parseEngineLine(const char* line,
                            CameraConfig& cam, PhysicsConfig& phy,
                            NetworkConfig& net, WorldConfig& wld) {
    parseLine(line, "camera.distance",    cam.distance);
    parseLine(line, "camera.height",      cam.height);
    parseLine(line, "camera.pitch",       cam.pitch);
    parseLine(line, "camera.fovY",        cam.fovY);
    parseLine(line, "camera.nearZ",       cam.nearZ);
    parseLine(line, "camera.farZ",        cam.farZ);
    parseLine(line, "camera.sensitivity", cam.sensitivity);
    parseLine(line, "camera.pitchMin",    cam.pitchMin);
    parseLine(line, "camera.pitchMax",    cam.pitchMax);

    parseLineD(line, "physics.fixedDt",     phy.fixedDt);
    parseLineI(line, "physics.maxSubSteps", phy.maxSubSteps);
    parseLine(line, "physics.sleepLinear",  phy.sleepLinear);
    parseLine(line, "physics.sleepAngular", phy.sleepAngular);
    parseLine(line, "physics.sleepDelay",   phy.sleepDelay);

    int port = 0;
    if (parseLineI(line, "network.port", port)) net.port = (uint16_t)port;
    parseLineD(line, "network.sendRate",         net.sendRate);
    parseLineI(line, "network.maxSnapshotBoxes", net.maxSnapshotBoxes);

    parseLineU64(line, "world.seed",     wld.seed);
    parseLine(line,   "world.worldSize", wld.worldSize);
    parseLineI(line,  "world.heightN",  wld.heightN);
}

// Reads game-specific keys off one line. The legacy hud.* keys map onto the
// new GameplayConfig HUD fields, so an existing game.cfg keeps positioning the
// HUD exactly as before.
static void parseGameplayLine(const char* line, GameplayConfig& g) {
    parseLine(line, "gameplay.grabReach",    g.grabReach);
    parseLine(line, "gameplay.fallDistance", g.fallDistance);
    parseLine(line, "gameplay.grabStamina",  g.grabStamina);
    parseLineI(line, "gameplay.staminaSegs", g.staminaSegs);
    parseLine(line, "hud.x",     g.hudX);
    parseLine(line, "hud.y",     g.hudY);
    parseLine(line, "hud.scale", g.hudScale);
    parseLine(line, "hud.lineH", g.hudLineH);
}

// Runs one line-handler over every line of a file.
template <typename Fn>
static bool forEachLine(const std::string& path, Fn fn) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
        if (!n || line[0] == '#') continue;
        fn(line);
    }
    fclose(f);
    return true;
}

// ============================== EngineConfig ==============================
void EngineConfig::reset() { *this = EngineConfig{}; }

bool EngineConfig::load(const std::string& path) {
    reset();
    return forEachLine(path, [this](const char* l) {
        parseEngineLine(l, camera, physics, network, world);
    });
}

bool EngineConfig::save(const std::string& path) const {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fprintf(f, "# bipbip ENGINE config — the same for every game on this engine\n");
    fprintf(f, "camera.distance = " BIP_FMT_F "\n",    (double)camera.distance);
    fprintf(f, "camera.height = " BIP_FMT_F "\n",      (double)camera.height);
    fprintf(f, "camera.pitch = " BIP_FMT_F "\n",       (double)camera.pitch);
    fprintf(f, "camera.fovY = " BIP_FMT_F "\n",        (double)camera.fovY);
    fprintf(f, "camera.nearZ = " BIP_FMT_F "\n",       (double)camera.nearZ);
    fprintf(f, "camera.farZ = " BIP_FMT_F "\n",        (double)camera.farZ);
    fprintf(f, "camera.sensitivity = " BIP_FMT_F "\n", (double)camera.sensitivity);
    fprintf(f, "camera.pitchMin = " BIP_FMT_F "\n",    (double)camera.pitchMin);
    fprintf(f, "camera.pitchMax = " BIP_FMT_F "\n",    (double)camera.pitchMax);

    fprintf(f, "physics.fixedDt = " BIP_FMT_D "\n",     physics.fixedDt);
    fprintf(f, "physics.maxSubSteps = %d\n", physics.maxSubSteps);
    fprintf(f, "physics.sleepLinear = " BIP_FMT_F "\n", (double)physics.sleepLinear);
    fprintf(f, "physics.sleepAngular = " BIP_FMT_F "\n",(double)physics.sleepAngular);
    fprintf(f, "physics.sleepDelay = " BIP_FMT_F "\n",  (double)physics.sleepDelay);

    fprintf(f, "network.port = %d\n",             (int)network.port);
    fprintf(f, "network.sendRate = " BIP_FMT_D "\n",         network.sendRate);
    fprintf(f, "network.maxSnapshotBoxes = %d\n", network.maxSnapshotBoxes);

    fprintf(f, "world.seed = %llu\n",    (unsigned long long)world.seed);
    fprintf(f, "world.worldSize = " BIP_FMT_F "\n", (double)world.worldSize);
    fprintf(f, "world.heightN = %d\n",   world.heightN);
    fclose(f);
    return true;
}

// ============================= GameplayConfig =============================
void GameplayConfig::reset() { *this = GameplayConfig{}; }

bool GameplayConfig::load(const std::string& path) {
    reset();
    return forEachLine(path, [this](const char* l) { parseGameplayLine(l, *this); });
}

bool GameplayConfig::save(const std::string& path) const {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fprintf(f, "# bipbip GAME config — swap this file to make a different game\n");
    fprintf(f, "gameplay.grabReach = " BIP_FMT_F "\n",    (double)grabReach);
    fprintf(f, "gameplay.fallDistance = " BIP_FMT_F "\n", (double)fallDistance);
    fprintf(f, "gameplay.grabStamina = " BIP_FMT_F "\n",  (double)grabStamina);
    fprintf(f, "gameplay.staminaSegs = %d\n",  staminaSegs);
    fprintf(f, "hud.x = " BIP_FMT_F "\n",     (double)hudX);
    fprintf(f, "hud.y = " BIP_FMT_F "\n",     (double)hudY);
    fprintf(f, "hud.scale = " BIP_FMT_F "\n", (double)hudScale);
    fprintf(f, "hud.lineH = " BIP_FMT_F "\n", (double)hudLineH);
    fclose(f);
    return true;
}

// ======================= legacy combined GameConfig =======================
void GameConfig::reset() {
    *this = GameConfig{};
    hud.x = gameplay.hudX; hud.y = gameplay.hudY;
    hud.scale = gameplay.hudScale; hud.lineH = gameplay.hudLineH;
}

bool GameConfig::load(const std::string& path) {
    reset();
    bool ok = forEachLine(path, [this](const char* l) {
        parseEngineLine(l, camera, physics, network, world);
        parseGameplayLine(l, gameplay);
    });
    hud.x = gameplay.hudX; hud.y = gameplay.hudY;
    hud.scale = gameplay.hudScale; hud.lineH = gameplay.hudLineH;
    return ok;
}

void GameConfig::applyEngine(const EngineConfig& e) {
    camera = e.camera; physics = e.physics; network = e.network; world = e.world;
}

void GameConfig::applyGameplay(const GameplayConfig& g) {
    gameplay = g;
    hud.x = g.hudX; hud.y = g.hudY; hud.scale = g.hudScale; hud.lineH = g.hudLineH;
}

// (GameConfig::save — legacy combined writer, kept for the shipped game.cfg)
bool GameConfig::save(const std::string& path) const {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fprintf(f, "# bipbip config — values are the game's tuned defaults\n");
    fprintf(f, "camera.distance = " BIP_FMT_F "\n",   (double)camera.distance);
    fprintf(f, "camera.height = " BIP_FMT_F "\n",     (double)camera.height);
    fprintf(f, "camera.pitch = " BIP_FMT_F "\n",      (double)camera.pitch);
    fprintf(f, "camera.fovY = " BIP_FMT_F "\n",       (double)camera.fovY);
    fprintf(f, "camera.nearZ = " BIP_FMT_F "\n",      (double)camera.nearZ);
    fprintf(f, "camera.farZ = " BIP_FMT_F "\n",       (double)camera.farZ);
    fprintf(f, "camera.sensitivity = " BIP_FMT_F "\n",(double)camera.sensitivity);
    fprintf(f, "camera.pitchMin = " BIP_FMT_F "\n",   (double)camera.pitchMin);
    fprintf(f, "camera.pitchMax = " BIP_FMT_F "\n",   (double)camera.pitchMax);

    fprintf(f, "physics.fixedDt = " BIP_FMT_D "\n",     physics.fixedDt);
    fprintf(f, "physics.maxSubSteps = %d\n", physics.maxSubSteps);
    fprintf(f, "physics.sleepLinear = " BIP_FMT_F "\n", (double)physics.sleepLinear);
    fprintf(f, "physics.sleepAngular = " BIP_FMT_F "\n",(double)physics.sleepAngular);
    fprintf(f, "physics.sleepDelay = " BIP_FMT_F "\n",  (double)physics.sleepDelay);

    fprintf(f, "gameplay.grabReach = " BIP_FMT_F "\n",    (double)gameplay.grabReach);
    fprintf(f, "gameplay.fallDistance = " BIP_FMT_F "\n", (double)gameplay.fallDistance);
    fprintf(f, "gameplay.grabStamina = " BIP_FMT_F "\n",  (double)gameplay.grabStamina);
    fprintf(f, "gameplay.staminaSegs = %d\n",  gameplay.staminaSegs);

    fprintf(f, "network.port = %d\n",            (int)network.port);
    fprintf(f, "network.sendRate = " BIP_FMT_D "\n",        network.sendRate);
    fprintf(f, "network.maxSnapshotBoxes = %d\n",network.maxSnapshotBoxes);

    fprintf(f, "hud.x = " BIP_FMT_F "\n",     (double)hud.x);
    fprintf(f, "hud.y = " BIP_FMT_F "\n",     (double)hud.y);
    fprintf(f, "hud.scale = " BIP_FMT_F "\n", (double)hud.scale);
    fprintf(f, "hud.lineH = " BIP_FMT_F "\n", (double)hud.lineH);

    fprintf(f, "world.seed = %llu\n",    (unsigned long long)world.seed);
    fprintf(f, "world.worldSize = " BIP_FMT_F "\n", (double)world.worldSize);
    fprintf(f, "world.heightN = %d\n",   world.heightN);
    fclose(f);
    return true;
}

} // namespace bip
