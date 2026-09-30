// Asset pipeline scaffold test (offline, no D3D).
// Verifies OBJ parsing + file-watcher logic without spinning up the renderer.
//
// The watcher test writes to a scratch COPY, never to assets/. The previous
// version edited the real assets/meshes/climber.obj and then tried to restore
// it, which left stray whitespace in the working tree and dirtied git on every
// run. A test must not mutate the repository it runs in.
#include "render/obj_loader.h"
#include "render/mesh.h"
#include "core/asset_watch.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <io.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++failures; } } while (0)

static bool copyFile(const char* from, const char* to) {
    FILE* g = fopen(from, "rb");
    if (!g) return false;
    FILE* h = fopen(to, "wb");
    if (!h) { fclose(g); return false; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), g)) > 0) fwrite(buf, 1, n, h);
    fclose(g); fclose(h);
    return true;
}

int main() {
    // 1) OBJ load: sample climber has 8 positions, 12 triangles -> 36 indices.
    std::vector<bip::Vertex> verts;
    std::vector<uint32_t> idx;
    bool ok = bip::loadObj("assets/meshes/climber.obj", verts, idx, true);
    CHECK(ok, "loadObj returns true for sample climber");
    CHECK(idx.size() == 36, "climber has 36 indices (12 tris)");
    CHECK(!verts.empty(), "climber produced vertices");
    // centered+scaled: bounding box should be roughly within [-1, 1].
    float maxc = 0.f;
    for (auto& v : verts) {
        for (int k = 0; k < 3; ++k) maxc = (std::abs(v.pos[k]) > maxc) ? std::abs(v.pos[k]) : maxc;
    }
    CHECK(maxc <= 1.05f, "climber normalized to ~unit size");

    // 2) Missing file -> false.
    std::vector<bip::Vertex> v2; std::vector<uint32_t> i2;
    CHECK(!bip::loadObj("assets/meshes/does_not_exist.obj", v2, i2, true),
          "loadObj false on missing file");

    // 3) AssetWatcher: seeds baseline, reports change only after mtime moves.
    //    All writes go to a scratch copy, never to the authored asset.
    const char* tmpObj = "bipbip_asset_watch_test.obj";
    if (!copyFile("assets/meshes/climber.obj", tmpObj)) {
        printf("FAIL: could not stage temp copy for watcher test\n");
        ++failures;
    } else {
        bip::AssetWatcher w;
        w.watch(tmpObj);
        auto first = w.poll();
        CHECK(first.empty(), "no change immediately after watch()");

        // simulate an edit: append a byte so content AND mtime both change
        {
            FILE* f = fopen(tmpObj, "a");
            if (f) { fputc('\n', f); fclose(f); }
        }
        auto changed = w.poll();
        CHECK(!changed.empty(), "watcher reports change after file edit");
        remove(tmpObj);
    }

    if (failures == 0) { printf("ASSET_PIPELINE_TEST_PASS\n"); return 0; }
    printf("ASSET_PIPELINE_TEST_FAIL (%d)\n", failures);
    return 1;
}
