// Scene sanity test: authored objects must sit ON the terrain, not inside it.
// This is what the user saw: boxes authored at fixed Y ended up underground
// because the mountain is ~48m tall at the centre.
#include "core/scene.h"
#include "world/heightfield.h"
#include <cstdio>
#include <cmath>

static int failures = 0;
#define CHECK(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); ++failures; } } while(0)

int main() {
    using namespace bip;

    Scene sc;
    if (!loadScene("assets/scenes/default.bipscene", sc)) {
        printf("FAIL: cannot load assets/scenes/default.bipscene\n");
        return 1;
    }
    HeightField hf;
    hf.generate(sc.seed, sc.worldSize, sc.heightN);

    printf("loaded scene: boxes=%zu holds=%zu\n", sc.boxes.size(), sc.holds.size());
    CHECK(!sc.boxes.empty(), "scene has boxes");
    CHECK(!sc.holds.empty(), "scene has holds");

    // every box must rest on/above the terrain surface
    for (size_t i = 0; i < sc.boxes.size(); ++i) {
        const auto& b = sc.boxes[i];
        float ground = hf.heightAt(b.pos[0], b.pos[2]);
        float bottom = b.pos[1] - b.half[1];   // lowest face of the box
        printf("box %zu: terrain=%.2f bottom=%.2f  -> %s\n",
               i, (double)ground, (double)bottom,
               bottom >= ground - 0.2f ? "ON GROUND" : "UNDERGROUND");
        CHECK(bottom >= ground - 0.2f, "box is not buried in the terrain");
    }

    // holds should be reachable: near the surface, not deep inside
    for (size_t i = 0; i < sc.holds.size(); ++i) {
        const auto& h = sc.holds[i];
        float ground = hf.heightAt(h.pos[0], h.pos[2]);
        float dy = h.pos[1] - ground;
        printf("hold %zu: terrain=%.2f y=%.2f dy=%.2f\n",
               i, (double)ground, (double)h.pos[1], (double)dy);
        CHECK(dy > -0.5f && dy < 8.f, "hold sits just above the terrain");
    }

    if (failures == 0) { printf("SCENE_GROUND_TEST_PASS\n"); return 0; }
    printf("SCENE_GROUND_TEST_FAIL (%d)\n", failures);
    return 1;
}
