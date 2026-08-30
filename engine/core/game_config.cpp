#include "core/game_config.h"
#include <cstdio>
#include <cstring>

namespace bip {

void GameConfig::reset() {
    *this = GameConfig{};   // struct defaults are the documented baseline
}

// Minimal key = value parser. Unknown keys are skipped rather than rejected so
// a config written by a newer/older build still loads.
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

bool GameConfig::load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    reset();
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        // strip trailing newline
        size_t n = strlen(line);
        while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
        if (!n || line[0] == '#') continue;

        parseLine(line, "camera.distance",   camera.distance);
        parseLine(line, "camera.height",     camera.height);
        parseLine(line, "camera.pitch",      camera.pitch);
        parseLine(line, "camera.fovY",       camera.fovY);
        parseLine(line, "camera.nearZ",      camera.nearZ);
        parseLine(line, "camera.farZ",       camera.farZ);
        parseLine(line, "camera.sensitivity",camera.sensitivity);
        parseLine(line, "camera.pitchMin",   camera.pitchMin);
        parseLine(line, "camera.pitchMax",   camera.pitchMax);

        parseLineD(line, "physics.fixedDt",      physics.fixedDt);
        parseLineI(line, "physics.maxSubSteps",  physics.maxSubSteps);
        parseLine(line, "physics.sleepLinear",   physics.sleepLinear);
        parseLine(line, "physics.sleepAngular",  physics.sleepAngular);
        parseLine(line, "physics.sleepDelay",    physics.sleepDelay);

        parseLine(line, "gameplay.grabReach",    gameplay.grabReach);
        parseLine(line, "gameplay.fallDistance", gameplay.fallDistance);
        parseLine(line, "gameplay.grabStamina",  gameplay.grabStamina);
        parseLineI(line, "gameplay.staminaSegs", gameplay.staminaSegs);

        int port = 0;
        if (parseLineI(line, "network.port", port)) network.port = (uint16_t)port;
        parseLineD(line, "network.sendRate", network.sendRate);
        parseLineI(line, "network.maxSnapshotBoxes", network.maxSnapshotBoxes);

        parseLine(line, "hud.x",      hud.x);
        parseLine(line, "hud.y",      hud.y);
        parseLine(line, "hud.scale",  hud.scale);
        parseLine(line, "hud.lineH",  hud.lineH);

        parseLineU64(line, "world.seed",      world.seed);
        parseLine(line, "world.worldSize",    world.worldSize);
        parseLineI(line, "world.heightN",     world.heightN);
    }
    fclose(f);
    return true;
}

bool GameConfig::save(const std::string& path) const {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fprintf(f, "# bipbip config — values are the game's tuned defaults\n");
    fprintf(f, "camera.distance = %g\n",   (double)camera.distance);
    fprintf(f, "camera.height = %g\n",     (double)camera.height);
    fprintf(f, "camera.pitch = %g\n",      (double)camera.pitch);
    fprintf(f, "camera.fovY = %g\n",       (double)camera.fovY);
    fprintf(f, "camera.nearZ = %g\n",      (double)camera.nearZ);
    fprintf(f, "camera.farZ = %g\n",       (double)camera.farZ);
    fprintf(f, "camera.sensitivity = %g\n",(double)camera.sensitivity);
    fprintf(f, "camera.pitchMin = %g\n",   (double)camera.pitchMin);
    fprintf(f, "camera.pitchMax = %g\n",   (double)camera.pitchMax);

    fprintf(f, "physics.fixedDt = %g\n",     physics.fixedDt);
    fprintf(f, "physics.maxSubSteps = %d\n", physics.maxSubSteps);
    fprintf(f, "physics.sleepLinear = %g\n", (double)physics.sleepLinear);
    fprintf(f, "physics.sleepAngular = %g\n",(double)physics.sleepAngular);
    fprintf(f, "physics.sleepDelay = %g\n",  (double)physics.sleepDelay);

    fprintf(f, "gameplay.grabReach = %g\n",    (double)gameplay.grabReach);
    fprintf(f, "gameplay.fallDistance = %g\n", (double)gameplay.fallDistance);
    fprintf(f, "gameplay.grabStamina = %g\n",  (double)gameplay.grabStamina);
    fprintf(f, "gameplay.staminaSegs = %d\n",  gameplay.staminaSegs);

    fprintf(f, "network.port = %d\n",            (int)network.port);
    fprintf(f, "network.sendRate = %g\n",        network.sendRate);
    fprintf(f, "network.maxSnapshotBoxes = %d\n",network.maxSnapshotBoxes);

    fprintf(f, "hud.x = %g\n",     (double)hud.x);
    fprintf(f, "hud.y = %g\n",     (double)hud.y);
    fprintf(f, "hud.scale = %g\n", (double)hud.scale);
    fprintf(f, "hud.lineH = %g\n", (double)hud.lineH);

    fprintf(f, "world.seed = %llu\n",    (unsigned long long)world.seed);
    fprintf(f, "world.worldSize = %g\n", (double)world.worldSize);
    fprintf(f, "world.heightN = %d\n",   world.heightN);
    fclose(f);
    return true;
}

} // namespace bip
