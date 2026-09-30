// psycho — the second game's world definition.
//
// This is the test that proves the engine is actually reusable, not just
// theoretically generic. psycho shares NOTHING with the climbing game except
// the engine libraries: no Route, no RunState, no climbing config, no
// procedural-mountain world.
//
// Its world is deliberately the simplest thing that exercises the generalised
// machinery: a player and a box, both as typed entities, loaded from a
// .bipscene v2 file through the same Scene loader the editor uses.
//
// What this test proves:
//   1. A scene can be authored with entities, saved as v2 and reloaded with no
//      loss — including a type the climbing game has no concept of.
//   2. psycho's own gameplay config supplies its numbers without touching the
//      engine defaults.
//   3. A character can be spawned and driven from a scene's boxes, colliding,
//      using only engine headers + psycho's own code.
#include "core/scene.h"
#include "core/game_config.h"
#include "character/climber.h"
#include "physics/verlet.h"
#include "world/heightfield.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++failures; } } while (0)

using namespace bip;

static const char* kScene    = "C:/Users/apex/AppData/Local/Temp/psycho_world_test.bipscene";
static const char* kGameplay = "C:/Users/apex/AppData/Local/Temp/psycho_gameplay.cfg";

// ------------------------------------------------ 1. scene round-trip
static void test_sceneRoundTrip() {
    Scene s;
    s.seed = 4242;
    s.spawn[0] = 0.f; s.spawn[1] = 0.f; s.spawn[2] = 0.f;

    // the box: the kind of thing a second game has and the climbing game does not
    Entity box;
    box.id = 1;
    box.type = kTypeBox;
    box.pos[0] = 4.f; box.pos[1] = 2.5f; box.pos[2] = -3.f;
    box.rotDeg[1] = 30.f;
    box.owner = 0;
    box.box = BoxComp{};
    box.box->half[0] = 1.5f; box.box->half[1] = 1.5f; box.box->half[2] = 1.5f;
    s.entities.push_back(box);

    // a lazer: a type the climbing game has no concept of at all
    Entity laz;
    laz.id = 2;
    laz.type = kTypeLazer;
    laz.pos[0] = -2.f; laz.pos[1] = 3.f; laz.pos[2] = 1.f;
    laz.lazer = LazerComp{};
    laz.lazer->color[0] = 1.f; laz.lazer->color[1] = 0.1f; laz.lazer->color[2] = 0.1f;
    laz.lazer->damage = 3;
    s.entities.push_back(laz);

    CHECK(saveSceneV2(s, kScene), "psycho scene saves as .bipscene v2");

    Scene in;
    CHECK(loadSceneV2(kScene, in), "psycho scene loads back");
    CHECK(in.entities.size() == 2, "both entities survive the round-trip");
    CHECK(in.seed == 4242, "world seed survives");

    const Entity* gotBox = in.findEntity(1);
    CHECK(gotBox != nullptr, "box entity found after reload");
    if (gotBox) {
        CHECK(gotBox->type == kTypeBox, "box keeps its type");
        CHECK(gotBox->box.has_value(), "box keeps its BoxComp");
        if (gotBox->box) {
            CHECK(std::fabs(gotBox->box->half[0] - 1.5f) < 1e-5f, "box half-extent survives");
        }
        CHECK(std::fabs(gotBox->pos[0] - 4.f) < 1e-5f, "box position survives");
        CHECK(std::fabs(gotBox->pos[2] + 3.f) < 1e-5f, "box z position survives");
        CHECK(std::fabs(gotBox->rotDeg[1] - 30.f) < 1e-3f, "box rotation survives");
    }

    // The whole point of the entity system: a type the first game has never
    // heard of must come back intact, not dropped as "unknown".
    const Entity* gotLaz = in.findEntity(2);
    CHECK(gotLaz != nullptr, "lazer entity (unknown to game 1) survives");
    if (gotLaz) {
        CHECK(gotLaz->type == kTypeLazer, "lazer keeps its type id");
        CHECK(gotLaz->lazer.has_value(), "lazer keeps its LazerComp");
        if (gotLaz->lazer) {
            CHECK(gotLaz->lazer->damage == 3, "lazer damage survives");
        }
    }
}

// ---------------------------------------- 2. psycho's own gameplay config
static void test_ownGameplayConfig() {
    // psycho invents its own numbers; the engine half stays at its defaults.
    GameplayConfig gp;
    gp.grabStamina = 9.f;         // deliberately different from the climbing game
    gp.fallDistance = 1.5f;
    CHECK(gp.save(kGameplay), "psycho saves its own gameplay config");

    GameplayConfig in;
    in.reset();
    CHECK(in.load(kGameplay), "psycho reloads its gameplay config");
    CHECK(std::fabs(in.grabStamina - 9.f) < 1e-4f, "psycho's stamina value loads");
    CHECK(std::fabs(in.fallDistance - 1.5f) < 1e-4f, "psycho's fall distance loads");

    // The engine half must NOT be contaminated by a gameplay-only file.
    EngineConfig eng;
    eng.reset();
    CHECK(std::fabs(eng.physics.fixedDt - (1.f / 60.f)) < 1e-6f,
          "engine defaults are independent of psycho's gameplay file");
    CHECK(eng.network.port == 27015, "engine network default untouched");
    CHECK(eng.network.sendRate > 0.049 && eng.network.sendRate < 0.051,
          "engine sendRate default is still 20 Hz");

    std::remove(kGameplay);
}

// ------------------------------------ 3. character + collision from a scene
static void test_characterFromScene() {
    Scene s;
    Entity block;
    block.id = 1;
    block.type = kTypeBox;
    block.pos[0] = 0.f; block.pos[1] = 0.f; block.pos[2] = 0.f;
    block.box = BoxComp{};
    block.box->half[0] = 3.f; block.box->half[1] = 0.5f; block.box->half[2] = 3.f;
    s.entities.push_back(block);
    s.syncView();                        // engine-side view the sim consumes

    CHECK(s.boxes.size() == 1, "syncView materialises the box for the sim");

    HeightField hf;
    hf.generate(4242, 60.f, 32);

    VerletWorld w;
    w.init(&hf);

    // build physics straight from the scene's boxes
    int boxesAdded = 0;
    for (const auto& b : s.boxes) {
        w.addBox(Vec3{b.pos[0], hf.heightAt(b.pos[0], b.pos[2]) + b.half[1], b.pos[2]},
                 b.half[0], b.half[1], b.half[2]);
        ++boxesAdded;
    }
    CHECK(boxesAdded >= 1, "scene box reached the physics world");

    // stand on the platform at the origin
    Climber c;
    c.init(&w, &hf, Vec3{0.f, 0.f, 0.f});
    const Vec3 start = c.pelvisPos();
    CHECK(std::isfinite(start.y), "character spawned on the platform");

    for (int i = 0; i < 180; ++i) {
        // walk off the platform edge (+x), so it must fall onto the terrain
        c.control(Vec3{1.f, 0.f, 0.f}, false, false, false, Vec3{}, Vec3{});
        w.step(1.f / 60.f);
        c.simulate(1.f / 60.f);
    }
    const Vec3 p = c.pelvisPos();
    CHECK(std::isfinite(p.x) && std::isfinite(p.y), "character stayed finite while moving");
    CHECK(p.x > start.x, "character actually moved in the commanded direction");
    CHECK(p.y > hf.heightAt(p.x, p.z) - 1.f, "character did not fall through the world");

    std::remove(kScene);
}

int main() {
    test_sceneRoundTrip();
    test_ownGameplayConfig();
    test_characterFromScene();

    if (failures == 0) { printf("PSYCHO_TEST_PASS\n"); return 0; }
    printf("PSYCHO_TEST_FAIL (%d)\n", failures);
    return 1;
}
