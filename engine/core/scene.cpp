#include "core/scene.h"
#include <cstdio>
#include <cstring>

namespace bip {

bool saveScene(const Scene& s, const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fprintf(f, "seed %llu\n", (unsigned long long)s.seed);
    fprintf(f, "world %g %d\n", (double)s.worldSize, s.heightN);
    fprintf(f, "spawn %g %g %g\n",
            (double)s.spawn[0], (double)s.spawn[1], (double)s.spawn[2]);
    for (const auto& b : s.boxes) {
        fprintf(f, "box %g %g %g  %g %g %g  %g %g %g  %d\n",
                (double)b.pos[0], (double)b.pos[1], (double)b.pos[2],
                (double)b.half[0], (double)b.half[1], (double)b.half[2],
                (double)b.rotDeg[0], (double)b.rotDeg[1], (double)b.rotDeg[2],
                b.owner);
    }
    for (const auto& h : s.holds) {
        fprintf(f, "hold %g %g %g  %d\n",
                (double)h.pos[0], (double)h.pos[1], (double)h.pos[2],
                h.checkpoint ? 1 : 0);
    }
    fclose(f);
    return true;
}

bool loadScene(const std::string& path, Scene& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out = Scene{};
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "seed ", 5)) {
            unsigned long long v = 0;
            if (sscanf(line + 5, "%llu", &v) == 1) out.seed = v;
        } else if (!strncmp(line, "world ", 6)) {
            float sz = 220.f; int n = 256;
            if (sscanf(line + 6, "%f %d", &sz, &n) == 2) {
                out.worldSize = sz; out.heightN = n;
            }
        } else if (!strncmp(line, "spawn ", 6)) {
            float a = 0, b = 0, c = 0;
            if (sscanf(line + 6, "%f %f %f", &a, &b, &c) == 3) {
                out.spawn[0] = a; out.spawn[1] = b; out.spawn[2] = c;
            }
        } else if (!strncmp(line, "box ", 4)) {
            SceneBox b;
            int own = 0;
            if (sscanf(line + 4, "%f %f %f %f %f %f %f %f %f %d",
                       &b.pos[0], &b.pos[1], &b.pos[2],
                       &b.half[0], &b.half[1], &b.half[2],
                       &b.rotDeg[0], &b.rotDeg[1], &b.rotDeg[2], &own) >= 9) {
                b.owner = own;
                out.boxes.push_back(b);
            }
        } else if (!strncmp(line, "hold ", 5)) {
            SceneHold h;
            int cp = 0;
            if (sscanf(line + 5, "%f %f %f %d",
                       &h.pos[0], &h.pos[1], &h.pos[2], &cp) >= 3) {
                h.checkpoint = (cp != 0);
                out.holds.push_back(h);
            }
        }
    }
    fclose(f);
    return true;
}

} // namespace bip
