// GameConfig test: defaults must match the game's current tuning, and a
// save/load round trip must preserve every value.
#include "core/game_config.h"
#include <cstdio>
#include <cmath>
#include <string>

static int failures = 0;
#define CHECK(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); ++failures; } } while(0)
#define NEAR(a,b) (std::fabs((double)(a)-(double)(b)) < 1e-4)

int main() {
    using namespace bip;

    GameConfig d;   // defaults
    // These are the exact values previously hardcoded in main.cpp
    CHECK(NEAR(d.camera.distance, 6.5f),  "default chase distance 6.5");
    CHECK(NEAR(d.camera.height, 1.6f),    "default camera height 1.6");
    CHECK(NEAR(d.camera.pitch, 0.32f),    "default camera pitch 0.32");
    CHECK(NEAR(d.camera.sensitivity, 0.003f), "default mouse sensitivity 0.003");
    CHECK(NEAR(d.physics.fixedDt, 1.0/60.0), "default fixed dt 1/60");
    CHECK(d.physics.maxSubSteps == 4,     "default max substeps 4");
    CHECK(NEAR(d.gameplay.grabReach, 2.2f),   "default grab reach 2.2");
    CHECK(NEAR(d.gameplay.fallDistance, 14.f),"default fall distance 14");
    CHECK(d.network.port == 27015,        "default port 27015");
    CHECK(NEAR(d.network.sendRate, 1.0/20.0), "default net rate 20Hz");
    CHECK(d.world.seed == 1337,           "default seed 1337");
    CHECK(NEAR(d.hud.x, 16.f),            "default hud x 16");
    CHECK(NEAR(d.hud.y, 14.f),            "default hud y 14");

    // round trip
    const std::string path = "C:/Users/apex/AppData/Local/Temp/test_cfg.txt";
    GameConfig src;
    src.camera.distance = 9.25f;
    src.camera.fovY = 1.4f;
    src.physics.fixedDt = 1.0 / 90.0;
    src.physics.maxSubSteps = 7;
    src.gameplay.grabReach = 3.5f;
    src.network.port = 12345;
    src.network.sendRate = 1.0 / 30.0;
    src.world.seed = 999888777;
    src.world.worldSize = 512.f;
    src.world.heightN = 512;
    src.hud.x = 64.f;
    src.hud.scale = 3.f;

    CHECK(src.save(path), "save config");
    GameConfig loaded;
    CHECK(loaded.load(path), "load config");

    CHECK(NEAR(loaded.camera.distance, 9.25f), "camera.distance preserved");
    CHECK(NEAR(loaded.camera.fovY, 1.4f),      "camera.fovY preserved");
    CHECK(NEAR(loaded.physics.fixedDt, 1.0/90.0), "physics.fixedDt preserved");
    CHECK(loaded.physics.maxSubSteps == 7,      "physics.maxSubSteps preserved");
    CHECK(NEAR(loaded.gameplay.grabReach, 3.5f),"gameplay.grabReach preserved");
    CHECK(loaded.network.port == 12345,         "network.port preserved");
    CHECK(NEAR(loaded.network.sendRate, 1.0/30.0), "network.sendRate preserved");
    CHECK(loaded.world.seed == 999888777ull,    "world.seed preserved");
    CHECK(NEAR(loaded.world.worldSize, 512.f),  "world.worldSize preserved");
    CHECK(loaded.world.heightN == 512,          "world.heightN preserved");
    CHECK(NEAR(loaded.hud.x, 64.f),             "hud.x preserved");
    CHECK(NEAR(loaded.hud.scale, 3.f),          "hud.scale preserved");

    // untouched keys keep defaults
    CHECK(NEAR(loaded.camera.height, 1.6f), "untouched key keeps default");

    // missing file -> false
    GameConfig missing;
    CHECK(!missing.load("C:/Users/apex/AppData/Local/Temp/no_such_cfg.txt"),
          "load returns false for missing file");

    if (failures == 0) { printf("GAME_CONFIG_TEST_PASS\n"); return 0; }
    printf("GAME_CONFIG_TEST_FAIL (%d)\n", failures);
    return 1;
}
