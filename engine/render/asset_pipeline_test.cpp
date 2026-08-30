// Asset pipeline scaffold test (offline, no D3D).
// Verifies OBJ parsing + file-watcher logic without spinning up the renderer.
#include "render/obj_loader.h"
#include "render/mesh.h"
#include "core/asset_watch.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <io.h>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++failures; } } while (0)

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
    bip::AssetWatcher w;
    w.watch("assets/meshes/climber.obj");
    auto first = w.poll();
    CHECK(first.empty(), "no change immediately after watch()");
    // simulate an edit by bumping the file's mtime (cross-platform via utime)
    {
        // append a harmless newline so content differs AND mtime moves
        FILE* f = fopen("assets/meshes/climber.obj", "a");
        if (f) { fputc('\n', f); fclose(f); }
    }
    auto changed = w.poll();
    CHECK(!changed.empty(), "watcher reports change after file edit");
    // restore exact original content (drop the trailing newline we added)
    {
        FILE* g = fopen("assets/meshes/climber.obj", "rb");
        if (g) {
            fseek(g, 0, SEEK_END);
            long sz = ftell(g);
            fseek(g, 0, SEEK_SET);
            std::string buf((size_t)sz, '\0');
            size_t rd = fread(buf.data(), 1, (size_t)sz, g);
            fclose(g);
            if (rd > 0 && buf.back() == '\n') {
                FILE* h = fopen("assets/meshes/climber.obj", "wb");
                if (h) { fwrite(buf.data(), 1, rd - 1, h); fclose(h); }
            }
        }
    }

    if (failures == 0) { printf("ASSET_PIPELINE_TEST_PASS\n"); return 0; }
    printf("ASSET_PIPELINE_TEST_FAIL (%d)\n", failures);
    return 1;
}
