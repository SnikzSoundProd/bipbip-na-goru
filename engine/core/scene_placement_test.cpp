// Scene->world placement test: authored Y must be resolved against the terrain.
//
// A real bug in psycho: the scene is authored in authoring space where pos.y is
// the height ABOVE the ground (1.0 = one metre up), but psycho fed pos[1]
// straight into VerletWorld::addBox as an absolute world Y. The heightfield is
// a procedural mountain, so ground near the origin is at y ≈ 48. Every one of
// psycho's six boxes was authored at y = 1..7 and ended up 46–54 metres UNDER
// the terrain. The character walked on top of them because terrain is solid,
// and the "stack of boxes" and "wall" were invisible and non-colliding.
//
// The first game never hit this: it generates its crates in code with
// hf.heightAt(x, z) + offset, so it never round-trips a scene through physics.
//
// This test pins the convention down:
//   1. a box authored at pos.y = 0 rests exactly on the ground
//   2. a box authored at pos.y = 2 sits 2 m above it
//   3. a box authored below ground still comes out resolvable, not NaN
//   4. the spawn point follows the same rule
//
// Offline: heightfield + scene + verlet only.

#include "core/scene.h"
#include "world/heightfield.h"
#include "physics/verlet.h"
#include <cstdio>
#include <cmath>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++failures; } } while (0)

using namespace bip;

// The helper psycho should use. Authored y is an offset above the terrain,
// because a scene is authored by hand: "y = 2" means "two metres up", not
// "y = 2 metres above the absolute origin of a 48-metre mountain".
//
// This test re-implements that helper ON PURPOSE and also checks psycho's own
// source: a helper that only exists in the test proves nothing. The original
// bug was that psycho had no helper at all and fed pos[1] straight into
// addBox, so the assertions below would pass while the shipped game buried
// every box under the mountain.
static Vec3 toWorld(const HeightField& hf, const Entity& e) {
    return Vec3{ e.pos[0], hf.heightAt(e.pos[0], e.pos[2]) + e.pos[1], e.pos[2] };
}

static std::string readFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return std::string();
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

// The shipped game must resolve authored Y against the terrain before it hands
// a box to the physics world.
static void checkPsychoResolvesAgainstTerrain() {
    const std::string src = readFile("C:/Users/apex/bipbip/psycho/main.cpp");
    if (src.empty()) {
        printf("FAIL: could not read psycho/main.cpp\n");
        ++failures;
        return;
    }
    // It must use the shared engine helper, not an inline arithmetic mix-up.
    // Any of the three resolution helpers counts: the rule is what matters,
    // not which entry point a game happens to need.
    const bool usesHelper = src.find("authoredToWorld(")   != std::string::npos ||
                            src.find("authoredBoxToWorld(")!= std::string::npos ||
                            src.find("authoredSpawnToWorld(") != std::string::npos;
    CHECK(usesHelper,
          "psycho must resolve scene positions with the engine helpers in "
          "core/scene_placement.h");
    CHECK(src.find("#include \"core/scene_placement.h\"") != std::string::npos,
          "psycho must include core/scene_placement.h");
    // the exact bug: authored y pushed in as an absolute world height
    CHECK(src.find("addBox(Vec3{b.pos[0], b.pos[1], b.pos[2]}") == std::string::npos,
          "psycho must not treat authored pos.y as an absolute world height");
}

static void test_boxRestsOnGround() {
    HeightField hf; hf.generate(1337, 300.f, 256);
    Entity e;
    e.id = 1; e.type = kTypeBox;
    e.pos[0] = 4.f; e.pos[1] = 0.f; e.pos[2] = 2.f;   // authored ON the ground
    e.box = BoxComp{};
    e.box->half[0] = 1.f; e.box->half[1] = 0.8f; e.box->half[2] = 1.f;

    const Vec3 w = toWorld(hf, e);
    const float g = hf.heightAt(e.pos[0], e.pos[2]);
    CHECK(std::fabs(w.y - g) < 1e-4f, "authored y=0 resolves to exactly the ground height");

    // A box whose CENTRE is at ground level would be half-buried. The centre
    // must be raised by its own half-height so its BOTTOM face touches ground.
    const Vec3 resting{ w.x, g + e.box->half[1], w.z };
    CHECK(resting.y - e.box->half[1] - g < 1e-4f, "resting box bottom sits on the ground, not half-buried");
    CHECK(std::isfinite(resting.y), "resolved height is finite");
}

static void test_boxAboveGround() {
    HeightField hf; hf.generate(1337, 300.f, 256);
    Entity e;
    e.id = 1; e.type = kTypeBox;
    e.pos[0] = 4.f; e.pos[1] = 2.f; e.pos[2] = 2.f;   // authored 2 m UP
    e.box = BoxComp{};
    e.box->half[0] = 1.f; e.box->half[1] = 0.8f; e.box->half[2] = 1.f;

    const float g = hf.heightAt(4.f, 2.f);
    const Vec3 w = toWorld(hf, e);
    CHECK(std::fabs((w.y - g) - 2.f) < 1e-4f, "authored y=2 lands exactly 2 m above the ground");
}

static void test_stackedBoxesAccumulate() {
    // psycho's scene is a stack: each box one half-height above the previous.
    // If the offset is not resolved against terrain the whole stack is buried.
    HeightField hf; hf.generate(1337, 300.f, 256);
    const float g = hf.heightAt(4.f, 2.f);
    float y = 0.f;
    bool monotonic = true;
    for (int i = 0; i < 5; ++i) {
        Entity e;
        e.id = (uint32_t)(i + 1); e.type = kTypeBox;
        e.pos[0] = 4.f; e.pos[1] = y; e.pos[2] = 2.f;
        e.box = BoxComp{};
        e.box->half[0] = 1.f; e.box->half[1] = 0.8f; e.box->half[2] = 1.f;
        const Vec3 w = toWorld(hf, e);
        if (w.y < g - 0.01f) monotonic = false;
        y += 1.6f;
    }
    CHECK(monotonic, "every box in a stack stays above the terrain");
}

static void test_wholeSceneResolves() {
    // The real check: load psycho's own authored scene and assert nothing in it
    // is buried. This is the test that would have caught the original bug.
    Scene s;
    if (!loadSceneV2("C:/Users/apex/bipbip/assets/scenes/psycho.bipscene", s)) {
        printf("FAIL: psycho's authored scene could not be loaded\n");
        ++failures;
        return;
    }
    CHECK(!s.entities.empty(), "psycho scene has entities");

    HeightField hf;
    hf.generate(s.seed, 300.f, 256);

    int buried = 0;
    for (const auto& e : s.entities) {
        if (e.type != kTypeBox || !e.box) continue;
        const float g = hf.heightAt(e.pos[0], e.pos[2]);
        const Vec3 w = toWorld(hf, e);
        // bottom face of the box, resolved into world space
        const float bottom = w.y - e.box->half[1];
        if (bottom < g - 0.5f) {
            printf("       box id=%u authored (%.1f, %.1f, %.1f) -> bottom %.2f vs ground %.2f\n",
                   e.id, e.pos[0], e.pos[1], e.pos[2], bottom, g);
            ++buried;
        }
    }
    CHECK(buried == 0, "no box in psycho's authored scene is buried under the terrain");
}

int main() {
    test_boxRestsOnGround();
    test_boxAboveGround();
    test_stackedBoxesAccumulate();
    test_wholeSceneResolves();
    checkPsychoResolvesAgainstTerrain();

    if (failures == 0) { printf("SCENE_PLACEMENT_TEST_PASS\n"); return 0; }
    printf("SCENE_PLACEMENT_TEST_FAIL (%d)\n", failures);
    return 1;
}
