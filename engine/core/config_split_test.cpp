// Config split test (offline, no D3D).
//
// STEP 3: GameConfig currently mixes engine-wide knobs (fixedDt, sleep
// thresholds, sendRate, camera) with climbing-specific ones (grabReach,
// fallDistance, stamina). A second game must be able to ship its own tuning
// WITHOUT inheriting climbing values, and engine changes must not require
// editing a game file.
//
// This test asserts:
//   1) EngineConfig carries only engine knobs, and they default to the values
//      the current game already uses (loading must not change behaviour).
//   2) GameConfig is game-specific and round-trips independently.
//   3) The two load from SEPARATE files, so a second game can override one
//      without touching the other.
//   4) A second game's config file cannot leak climbing keys into the engine.
#include "core/game_config.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++failures; } } while (0)

using namespace bip;

// ------------------------------------------------------------------ engine
static void test_engineDefaultsMatchCurrentGame() {
    EngineConfig e;
    e.reset();
    // These MUST equal what the game does today, otherwise switching to
    // EngineConfig silently changes how the game plays.
    CHECK(std::fabs(e.physics.fixedDt - 1.0/60.0) < 1e-9, "engine fixedDt = 60Hz");
    CHECK(e.physics.maxSubSteps == 4, "engine maxSubSteps = 4");
    CHECK(std::fabs(e.physics.sleepLinear - 0.30f) < 1e-6f, "engine sleepLinear");
    CHECK(std::fabs(e.physics.sleepAngular - 0.40f) < 1e-6f, "engine sleepAngular");
    CHECK(std::fabs(e.physics.sleepDelay - 0.35f) < 1e-6f, "engine sleepDelay");
    CHECK(std::fabs(e.network.sendRate - 1.0/20.0) < 1e-9, "engine sendRate = 20Hz");
    CHECK(e.network.port == 27015, "engine port default");
    CHECK(std::fabs(e.camera.sensitivity - 0.003f) < 1e-6f, "engine camera sensitivity");
    CHECK(std::fabs(e.camera.distance - 6.5f) < 1e-6f, "engine camera distance");
    CHECK(std::fabs(e.camera.pitch - 0.32f) < 1e-6f, "engine camera pitch");
    CHECK(e.world.heightN == 256, "engine heightN");
}

static void test_engineRoundTrip() {
    EngineConfig e;
    e.reset();
    e.physics.fixedDt = 1.0/30.0;
    e.physics.sleepLinear = 0.9f;
    e.network.sendRate = 1.0/60.0;
    e.network.port = 30000;
    e.camera.sensitivity = 0.01f;
    e.world.heightN = 512;
    e.world.worldSize = 800.f;

    const char* p = "engine_split_test.cfg";
    CHECK(e.save(p), "engine config saves");
    EngineConfig in;
    CHECK(in.load(p), "engine config loads");
    CHECK(std::fabs(in.physics.fixedDt - 1.0/30.0) < 1e-9, "fixedDt round-trips");
    CHECK(std::fabs(in.physics.sleepLinear - 0.9f) < 1e-6f, "sleepLinear round-trips");
    CHECK(std::fabs(in.network.sendRate - 1.0/60.0) < 1e-9, "sendRate round-trips");
    CHECK(in.network.port == 30000, "port round-trips");
    CHECK(std::fabs(in.camera.sensitivity - 0.01f) < 1e-6f, "sensitivity round-trips");
    CHECK(in.world.heightN == 512, "heightN round-trips");
    CHECK(std::fabs(in.world.worldSize - 800.f) < 1e-3f, "worldSize round-trips");
    remove(p);
}

// -------------------------------------------------------------------- game
static void test_gameRoundTrip() {
    GameplayConfig g;
    g.reset();
    g.grabReach = 3.5f;
    g.fallDistance = 20.f;
    g.grabStamina = 4.f;
    g.staminaSegs = 30;
    g.hudX = 40.f; g.hudY = 50.f; g.hudScale = 3.f; g.hudLineH = 24.f;

    const char* p = "game_split_test.cfg";
    CHECK(g.save(p), "game config saves");
    GameplayConfig in;
    CHECK(in.load(p), "game config loads");
    CHECK(std::fabs(in.grabReach - 3.5f) < 1e-6f, "grabReach round-trips");
    CHECK(std::fabs(in.fallDistance - 20.f) < 1e-4f, "fallDistance round-trips");
    CHECK(std::fabs(in.grabStamina - 4.f) < 1e-6f, "grabStamina round-trips");
    CHECK(in.staminaSegs == 30, "staminaSegs round-trips");
    CHECK(std::fabs(in.hudX - 40.f) < 1e-6f, "hudX round-trips");
    CHECK(std::fabs(in.hudScale - 3.f) < 1e-6f, "hudScale round-trips");
    remove(p);
}

static void test_gameDefaultsMatchCurrentGame() {
    GameplayConfig g;
    g.reset();
    CHECK(std::fabs(g.grabReach - 2.2f) < 1e-6f, "grabReach default unchanged");
    CHECK(std::fabs(g.fallDistance - 14.f) < 1e-4f, "fallDistance default unchanged");
    CHECK(std::fabs(g.grabStamina - 2.f) < 1e-6f, "grabStamina default unchanged");
    CHECK(g.staminaSegs == 20, "staminaSegs default unchanged");
    CHECK(std::fabs(g.hudX - 16.f) < 1e-6f, "hudX default unchanged");
    CHECK(std::fabs(g.hudY - 14.f) < 1e-6f, "hudY default unchanged");
    CHECK(std::fabs(g.hudScale - 2.f) < 1e-6f, "hudScale default unchanged");
    CHECK(std::fabs(g.hudLineH - 20.f) < 1e-6f, "hudLineH default unchanged");
}

// ------------------------------------------------- independence / isolation
static void test_secondGameOverridesOnlyItsOwnFile() {
    // Write an engine file and a (hypothetical second) game file with DIFFERENT
    // values, load both, and assert neither bleeds into the other.
    const char* ep = "engine_ind_test.cfg";
    const char* gp = "game2_ind_test.cfg";

    EngineConfig e; e.reset();
    e.physics.fixedDt = 1.0/120.0;
    e.network.port = 40000;
    e.save(ep);

    // A second game that has nothing to do with climbing: no stamina at all.
    // It only cares about its own gameplay numbers.
    FILE* f = fopen(gp, "wb");
    CHECK(f != nullptr, "can write second game config");
    if (f) {
        fprintf(f, "# a completely different game\n");
        fprintf(f, "gameplay.timeLimit = 300\n");
        fprintf(f, "gameplay.lives = 3\n");
        fclose(f);
    }

    EngineConfig ein; ein.load(ep);
    GameplayConfig gin; gin.load(gp);

    CHECK(std::fabs(ein.physics.fixedDt - 1.0/120.0) < 1e-9, "engine file applied");
    CHECK(ein.network.port == 40000, "engine port from engine file");
    // the second game's file must not have touched engine values
    CHECK(std::fabs(ein.physics.fixedDt - 1.0/120.0) < 1e-9, "engine fixedDt unaffected by game file");
    // and its unknown keys must not corrupt climbing defaults
    CHECK(std::fabs(gin.grabReach - 2.2f) < 1e-6f, "unknown game keys leave grabReach at default");
    CHECK(gin.staminaSegs == 20, "unknown game keys leave staminaSegs at default");

    remove(ep); remove(gp);
}

static void test_climbingKeysDoNotLeakIntoEngine() {
    // Writing engine.save() must NOT emit gameplay.* keys any more, and an
    // engine file containing them must be ignored by EngineConfig::load.
    const char* p = "engine_leak_test.cfg";
    EngineConfig e; e.reset();
    e.save(p);

    FILE* f = fopen(p, "rb");
    CHECK(f != nullptr, "engine file readable");
    bool sawGameplay = false;
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f))
            if (strstr(line, "gameplay.")) { sawGameplay = true; break; }
        fclose(f);
    }
    CHECK(!sawGameplay, "engine config file contains no gameplay.* keys");

    // a polluted file still loads cleanly
    FILE* g = fopen(p, "ab");
    if (g) { fprintf(g, "gameplay.grabReach = 99\n"); fclose(g); }
    EngineConfig in;
    CHECK(in.load(p), "engine config loads a polluted file");
    CHECK(std::fabs(in.physics.fixedDt - 1.0/60.0) < 1e-9, "engine values still correct after pollution");
    remove(p);
}

static void test_legacyCombinedFileStillLoads() {
    // The SHIPPED game.cfg (combined format) must keep working for BOTH, so
    // existing user tweaks are not lost on upgrade.
    const char* p = "legacy_combined_test.cfg";
    FILE* f = fopen(p, "wb");
    CHECK(f != nullptr, "can write legacy combined file");
    if (f) {
        fprintf(f, "camera.distance = 7.25\n");
        fprintf(f, "physics.fixedDt = 0.02\n");
        fprintf(f, "gameplay.grabReach = 2.9\n");
        fprintf(f, "network.port = 27020\n");
        fclose(f);
    }
    EngineConfig e; GameplayConfig g;
    CHECK(e.load(p), "engine reads legacy combined file");
    CHECK(g.load(p), "game reads legacy combined file");
    CHECK(std::fabs(e.camera.distance - 7.25f) < 1e-6f, "legacy camera value picked up by engine");
    CHECK(std::fabs(e.physics.fixedDt - 0.02) < 1e-9, "legacy physics value picked up");
    CHECK(e.network.port == 27020, "legacy network value picked up");
    CHECK(std::fabs(g.grabReach - 2.9f) < 1e-6f, "legacy gameplay value picked up by game");
    remove(p);
}

int main() {
    test_engineDefaultsMatchCurrentGame();
    test_engineRoundTrip();
    test_gameRoundTrip();
    test_gameDefaultsMatchCurrentGame();
    test_secondGameOverridesOnlyItsOwnFile();
    test_climbingKeysDoNotLeakIntoEngine();
    test_legacyCombinedFileStillLoads();

    if (failures == 0) { printf("CONFIG_SPLIT_TEST_PASS\n"); return 0; }
    printf("CONFIG_SPLIT_TEST_FAIL (%d)\n", failures);
    return 1;
}
