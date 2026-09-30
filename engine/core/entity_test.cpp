// Entity system test (offline, no D3D).
//
// STEP 1 of generalising the engine for a second game:
//   Scene keeps its existing boxes[]/holds[] view for the current game and the
//   editor (40 call sites), while `entities` becomes the source of truth.
//   syncDerived() rebuilds entities from the view; syncView() rebuilds the view
//   from entities. A second game can use entities directly and never touch
//   boxes/holds.
//
//   Test order follows the build order: entity storage -> view<->entities ->
//   filtering by type -> v2 file format.
#include "core/scene.h"
#include <cstdio>
#include <cmath>
#include <string>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++failures; } } while (0)

using namespace bip;

// ---------------------------------------------------------------- entities
static void test_entityBasics() {
    Scene s;
    s.syncDerived();
    CHECK(s.entities.empty(), "fresh scene has no entities");

    // A box in the view must materialise as a typed entity.
    SceneBox b{}; b.pos[0] = 1.f; b.pos[1] = 2.f; b.pos[2] = 3.f;
    b.half[0] = 0.5f; b.half[1] = 0.75f; b.half[2] = 1.f;
    b.rotDeg[1] = 90.f; b.owner = 1;
    s.boxes.push_back(b);

    s.syncDerived();
    CHECK(s.entities.size() == 1, "box became one entity");
    if (s.entities.empty()) return;

    const Entity& e = s.entities[0];
    CHECK(e.type == kTypeBox, "entity is typed box");
    CHECK(e.pos[0] == 1.f && e.pos[1] == 2.f && e.pos[2] == 3.f, "entity keeps pos");
    CHECK(e.rotDeg[1] == 90.f, "entity keeps rotation");
    CHECK(e.owner == 1, "entity keeps owner");
    CHECK(e.box.has_value(), "box component present");
    if (e.box) {
        CHECK(e.box->half[0] == 0.5f, "box half.x preserved");
        CHECK(e.box->half[1] == 0.75f, "box half.y preserved");
    }

    // holds become their own type
    SceneHold h{}; h.pos[0] = 7.f; h.checkpoint = true;
    s.holds.push_back(h);
    s.syncDerived();
    CHECK(s.entities.size() == 2, "hold became second entity");
    bool foundHold = false;
    for (const auto& en : s.entities)
        if (en.type == kTypeHold) {
            foundHold = true;
            CHECK(en.hold.has_value(), "hold component present");
            if (en.hold) CHECK(en.hold->checkpoint, "checkpoint flag preserved");
        }
    CHECK(foundHold, "hold entity exists");
}

static void test_idsUnique() {
    Scene s;
    SceneBox b{}; s.boxes.push_back(b);
    SceneBox b2{}; b2.pos[1] = 9.f; s.boxes.push_back(b2);
    SceneHold h{}; s.holds.push_back(h);
    s.syncDerived();
    for (size_t i = 0; i < s.entities.size(); ++i)
        for (size_t j = i + 1; j < s.entities.size(); ++j)
            CHECK(s.entities[i].id != s.entities[j].id, "entity ids are unique");
}

static void test_viewRoundTrip() {
    Scene s;
    SceneBox b{}; b.pos[0] = 4.f; b.pos[1] = 5.f; b.pos[2] = 6.f;
    b.half[0] = 1.f; b.half[1] = 2.f; b.half[2] = 3.f; b.owner = 1;
    s.boxes.push_back(b);
    SceneHold h{}; h.pos[0] = -1.f; h.pos[1] = 8.f; h.pos[2] = -2.f; h.checkpoint = true;
    s.holds.push_back(h);

    s.syncDerived();          // view -> entities
    s.boxes.clear();          // wipe the view
    s.holds.clear();
    CHECK(s.boxes.empty() && s.holds.empty(), "view cleared before rebuild");
    s.syncView();             // entities -> view

    CHECK(s.boxes.size() == 1, "box rebuilt from entities");
    CHECK(s.holds.size() == 1, "hold rebuilt from entities");
    if (s.boxes.size() == 1) {
        CHECK(s.boxes[0].pos[0] == 4.f && s.boxes[0].pos[2] == 6.f, "box pos round-trips");
        CHECK(s.boxes[0].half[1] == 2.f, "box half round-trips");
        CHECK(s.boxes[0].owner == 1, "box owner round-trips");
    }
    if (s.holds.size() == 1) {
        CHECK(s.holds[0].pos[1] == 8.f, "hold pos round-trips");
        CHECK(s.holds[0].checkpoint, "checkpoint round-trips");
    }
}

static void test_customTypeForSecondGame() {
    // This is the whole point: a NEW game adds a type without forking the
    // engine. A 'lazer' entity must survive sync cycles untouched.
    Scene s;
    Entity e;
    e.type = kTypeLazer;
    e.pos[0] = 10.f; e.pos[1] = 20.f; e.pos[2] = 30.f;
    LazerComp lz{}; lz.color[0] = 1.f; lz.color[1] = 0.f; lz.color[2] = 0.f; lz.damage = 7;
    e.lazer = lz;
    s.entities.push_back(e);

    s.syncView();     // a lazer is not a box/hold, so the view stays empty
    CHECK(s.boxes.empty(), "lazer does not appear in box view");
    CHECK(s.holds.empty(), "lazer does not appear in hold view");

    // and it must still be there, unchanged
    CHECK(s.entities.size() == 1, "lazer entity survived syncView");
    if (s.entities.size() == 1) {
        CHECK(s.entities[0].type == kTypeLazer, "lazer kept its type");
        CHECK(s.entities[0].lazer.has_value(), "lazer kept its component");
        if (s.entities[0].lazer) CHECK(s.entities[0].lazer->damage == 7, "lazer data intact");
    }
}

static void test_countByType() {
    Scene s;
    Entity a; a.type = kTypeBox;
    Entity b; a.type = kTypeBox; b.type = kTypeBox;
    Entity c; c.type = kTypeLazer;
    s.entities = {a, b, c};
    CHECK(countByType(s, kTypeBox) == 2, "counts boxes");
    CHECK(countByType(s, kTypeLazer) == 1, "counts lazers");
    CHECK(countByType(s, kTypeHold) == 0, "counts missing type as zero");
}

// ------------------------------------------------------- v2 file format
static void test_v2FormatRoundTrip() {
    Scene s;
    s.seed = 4242;
    s.worldSize = 300.f; s.heightN = 128;
    s.spawn[0] = 1.f; s.spawn[1] = 2.f; s.spawn[2] = 3.f;

    Entity box; box.id = 1; box.type = kTypeBox;
    box.pos[0] = 5.f; box.pos[1] = 6.f; box.pos[2] = 7.f;
    BoxComp bc{}; bc.half[0] = 0.5f; bc.half[1] = 1.5f; bc.half[2] = 2.5f;
    box.box = bc;
    s.entities.push_back(box);

    Entity lz; lz.id = 2; lz.type = kTypeLazer;
    lz.pos[0] = 9.f; lz.pos[1] = 8.f; lz.pos[2] = 7.f;
    LazerComp lc{}; lc.color[0] = 0.2f; lc.color[1] = 0.4f; lc.color[2] = 0.6f; lc.damage = 3;
    lz.lazer = lc;
    s.entities.push_back(lz);

    const char* path = "entity_scene_test.bipscene";
    CHECK(saveSceneV2(s, path), "saveSceneV2 writes a file");

    Scene out;
    CHECK(loadSceneV2(path, out), "loadSceneV2 reads it back");
    CHECK(out.seed == 4242, "seed round-trips");
    CHECK(out.worldSize == 300.f, "worldSize round-trips");
    CHECK(out.heightN == 128, "heightN round-trips");
    CHECK(out.spawn[0] == 1.f && out.spawn[2] == 3.f, "spawn round-trips");
    CHECK(out.entities.size() == 2, "both entities round-trip");

    if (out.entities.size() == 2) {
        // find the box
        bool sawBox = false, sawLazer = false;
        for (const auto& e : out.entities) {
            if (e.type == kTypeBox) {
                sawBox = true;
                CHECK(e.id == 1, "box id round-trips");
                CHECK(e.pos[1] == 6.f, "box pos round-trips");
                CHECK(e.box.has_value(), "box comp round-trips");
                if (e.box) CHECK(e.box->half[1] == 1.5f, "box half round-trips");
            } else if (e.type == kTypeLazer) {
                sawLazer = true;
                CHECK(e.id == 2, "lazer id round-trips");
                CHECK(e.lazer.has_value(), "lazer comp round-trips");
                if (e.lazer) {
                    CHECK(e.lazer->damage == 3, "lazer damage round-trips");
                    CHECK(std::fabs(e.lazer->color[1] - 0.4f) < 1e-3f, "lazer color round-trips");
                }
            }
        }
        CHECK(sawBox, "box entity found after load");
        CHECK(sawLazer, "lazer entity found after load");
    }
    remove(path);
}

static void test_v2UnknownKeysSkipped() {
    // A scene written by a NEWER build (with fields this build has never heard
    // of) must still load. This is what makes the format extensible.
    const char* path = "entity_scene_fwd.bipscene";
    FILE* f = fopen(path, "wb");
    CHECK(f != nullptr, "can create forward-compat scene");
    if (!f) return;
    fprintf(f, "bipscene 2\n");
    fprintf(f, "seed 7\n");
    fprintf(f, "world 300 256\n");
    fprintf(f, "spawn 0 0 118\n");
    fprintf(f, "entity 1 box pos=1 2 3 half=0.5 0.5 0.5 owner=0\n");
    fprintf(f, "entity 2 hold pos=4 5 6 checkpoint=1\n");
    fprintf(f, "entity 3 quantumpylon pos=1 1 1 wobble=0.77 flux=42\n");  // unknown type + keys
    fprintf(f, "future_directive something=1\n");                        // unknown line
    fclose(f);

    Scene out;
    CHECK(loadSceneV2(path, out), "loads scene with unknown types");
    CHECK(out.entities.size() == 3, "all three entities loaded");
    CHECK(out.seed == 7, "seed still parsed despite unknown lines");

    bool sawUnknown = false, sawHold = false;
    for (const auto& e : out.entities) {
        if (e.type == kTypeQuantumPylon) sawUnknown = true;
        if (e.type == kTypeHold) { sawHold = true; CHECK(e.hold.has_value(), "hold comp from key=value"); }
    }
    CHECK(sawUnknown, "unknown entity type preserved, not dropped");
    CHECK(sawHold, "hold entity loaded from key=value form");
    remove(path);
}

static void test_v1StillLoads() {
    // Backward compatibility: the existing game scene (v1, positional "box ...")
    // must still open and be upgraded to entities.
    Scene s;
    SceneBox b{}; b.pos[0] = 1.f; b.pos[1] = 2.f; b.pos[2] = 3.f; b.owner = 1;
    s.boxes.push_back(b);
    SceneHold h{}; h.pos[0] = 9.f; h.checkpoint = true;
    s.holds.push_back(h);
    const char* path = "entity_scene_v1.bipscene";
    CHECK(saveScene(s, path), "v1 save still works");

    Scene out;
    CHECK(loadSceneV2(path, out), "loadSceneV2 accepts a v1 file");
    CHECK(out.boxes.size() == 1, "v1 box still visible in view");
    CHECK(out.holds.size() == 1, "v1 hold still visible in view");
    // and it upgrades to entities so a new game can read old scenes
    out.syncDerived();
    CHECK(out.entities.size() == 2, "v1 scene upgraded to 2 entities");
    remove(path);
}

int main() {
    test_entityBasics();
    test_idsUnique();
    test_viewRoundTrip();
    test_customTypeForSecondGame();
    test_countByType();
    test_v2FormatRoundTrip();
    test_v2UnknownKeysSkipped();
    test_v1StillLoads();

    if (failures == 0) { printf("ENTITY_TEST_PASS\n"); return 0; }
    printf("ENTITY_TEST_FAIL (%d)\n", failures);
    return 1;
}
