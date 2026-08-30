// Scene serialization round-trip test (offline, no D3D).
// Verifies saveScene/loadScene preserve authored data exactly.
#include "core/scene.h"
#include <cstdio>
#include <cmath>
#include <string>

static int failures = 0;
#define CHECK(c, m) do { if(!(c)) { printf("FAIL: %s\n", m); ++failures; } } while(0)

int main() {
    const std::string path = "C:/Users/apex/AppData/Local/Temp/test_scene.bipscene";

    bip::Scene original;
    original.seed = 424242;
    original.worldSize = 300.f;
    original.heightN = 256;
    original.spawn[0] = 1.f; original.spawn[1] = 2.f; original.spawn[2] = 3.f;

    bip::SceneBox b0; b0.pos[0]=10.f; b0.pos[1]=20.f; b0.pos[2]=30.f;
    b0.half[0]=1.5f; b0.half[1]=2.5f; b0.half[2]=3.5f;
    b0.rotDeg[0]=15.f; b0.rotDeg[1]=45.f; b0.rotDeg[2]=90.f; b0.owner=1;
    original.boxes.push_back(b0);

    bip::SceneBox b1; b1.pos[0]=-5.f; b1.pos[1]=7.f; b1.pos[2]=-9.f;
    b1.half[0]=0.5f; b1.half[1]=0.5f; b1.half[2]=0.5f; b1.owner=0;
    original.boxes.push_back(b1);

    bip::SceneHold h0; h0.pos[0]=1.f; h0.pos[1]=2.f; h0.pos[2]=3.f; h0.checkpoint=true;
    bip::SceneHold h1; h1.pos[0]=4.f; h1.pos[1]=5.f; h1.pos[2]=6.f; h1.checkpoint=false;
    original.holds.push_back(h0);
    original.holds.push_back(h1);

    CHECK(bip::saveScene(original, path), "saveScene succeeds");

    bip::Scene loaded;
    CHECK(bip::loadScene(path, loaded), "loadScene succeeds");

    CHECK(loaded.seed == original.seed, "seed preserved");
    CHECK(std::fabs(loaded.worldSize - original.worldSize) < 0.01f, "worldSize preserved");
    CHECK(loaded.heightN == original.heightN, "heightN preserved");
    CHECK(loaded.boxes.size() == original.boxes.size(), "box count preserved");
    CHECK(loaded.holds.size() == original.holds.size(), "hold count preserved");

    if (loaded.boxes.size() == 2) {
        for (int k = 0; k < 3; ++k) {
            CHECK(std::fabs(loaded.boxes[0].pos[k] - b0.pos[k]) < 0.01f, "box0 pos preserved");
            CHECK(std::fabs(loaded.boxes[0].half[k] - b0.half[k]) < 0.01f, "box0 half preserved");
            CHECK(std::fabs(loaded.boxes[0].rotDeg[k] - b0.rotDeg[k]) < 0.01f, "box0 rot preserved");
        }
        CHECK(loaded.boxes[0].owner == 1, "box0 owner preserved");
        CHECK(loaded.boxes[1].owner == 0, "box1 owner preserved");
    }
    if (loaded.holds.size() == 2) {
        CHECK(loaded.holds[0].checkpoint == true, "hold0 checkpoint preserved");
        CHECK(loaded.holds[1].checkpoint == false, "hold1 checkpoint preserved");
        CHECK(std::fabs(loaded.holds[0].pos[0] - 1.f) < 0.01f, "hold0 pos preserved");
    }

    // spawn
    CHECK(std::fabs(loaded.spawn[0] - 1.f) < 0.01f, "spawn preserved");

    // missing file -> false
    bip::Scene missing;
    CHECK(!bip::loadScene("C:/Users/apex/AppData/Local/Temp/definitely_missing.bipscene", missing),
          "loadScene false on missing file");

    if (failures == 0) { printf("SCENE_ROUNDTRIP_TEST_PASS\n"); return 0; }
    printf("SCENE_ROUNDTRIP_TEST_FAIL (%d)\n", failures);
    return 1;
}
