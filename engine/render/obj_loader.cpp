#include "render/obj_loader.h"
#include "render/mesh.h"
#include "core/math.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <map>
#include <algorithm>

namespace bip {

bool loadObj(const std::string& path, std::vector<Vertex>& outVerts,
             std::vector<uint32_t>& outIndices, bool centerAndScale) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

    std::vector<float> px, py, pz;
    std::vector<float> nx, ny, nz;
    std::vector<float> u, v;

    char line[512];
    float minx = 1e9f, miny = 1e9f, minz = 1e9f;
    float maxx = -1e9f, maxy = -1e9f, maxz = -1e9f;

    // First pass: collect attribute data + bounds.
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'v' && line[1] == ' ') {
            float x, y, z;
            if (sscanf(line, "v %f %f %f", &x, &y, &z) == 3) {
                px.push_back(x); py.push_back(y); pz.push_back(z);
                if (x < minx) minx = x; if (y < miny) miny = y; if (z < minz) minz = z;
                if (x > maxx) maxx = x; if (y > maxy) maxy = y; if (z > maxz) maxz = z;
            }
        } else if (line[0] == 'v' && line[1] == 't') {
            float tu, tv;
            if (sscanf(line, "vt %f %f", &tu, &tv) == 2) { u.push_back(tu); v.push_back(tv); }
        } else if (line[0] == 'v' && line[1] == 'n') {
            float nxv, nyv, nzv;
            if (sscanf(line, "vn %f %f %f", &nxv, &nyv, &nzv) == 3) { nx.push_back(nxv); ny.push_back(nyv); nz.push_back(nzv); }
        }
    }
    rewind(f);

    if (px.empty()) { fclose(f); return false; }

    // Normalization transform.
    float cx = (minx + maxx) * 0.5f, cy = (miny + maxy) * 0.5f, cz = (minz + maxz) * 0.5f;
    float span = std::max({ (maxx - minx), (maxy - miny), (maxz - minz) });
    float scale = (span > 1e-5f) ? (2.0f / span) : 1.0f;

    // Vertex cache keyed by the face tuple "v/vt/vn" so shared positions with
    // different uvs/normals become distinct vertices (OBJ requires this).
    struct Key { int p, t, n; bool operator<(const Key& o) const {
        if (p != o.p) return p < o.p; if (t != o.t) return t < o.t; return n < o.n; } };
    std::map<Key, uint32_t> cache;

    auto getVertex = [&](int vi, int ti, int ni) -> uint32_t {
        Key k{ vi, ti, ni };
        auto it = cache.find(k);
        if (it != cache.end()) return it->second;
        Vertex vert{};
        int pi = vi - 1;
        if (centerAndScale) {
            vert.pos[0] = (px[pi] - cx) * scale;
            vert.pos[1] = (py[pi] - cy) * scale;
            vert.pos[2] = (pz[pi] - cz) * scale;
        } else {
            vert.pos[0] = px[pi]; vert.pos[1] = py[pi]; vert.pos[2] = pz[pi];
        }
        if (ni > 0 && (size_t)ni <= nz.size()) {
            vert.normal[0] = nx[ni - 1]; vert.normal[1] = ny[ni - 1]; vert.normal[2] = nz[ni - 1];
        } else {
            vert.normal[1] = 1.0f; // flat up as a safe default
        }
        if (ti > 0 && (size_t)ti <= u.size()) {
            vert.uv[0] = u[ti - 1]; vert.uv[1] = 1.0f - v[ti - 1]; // flip V for D3D
        }
        vert.color[0] = vert.color[1] = vert.color[2] = 1.0f;
        uint32_t idx = (uint32_t)outVerts.size();
        outVerts.push_back(vert);
        cache[k] = idx;
        return idx;
    };

    // Second pass: faces.
    size_t base = outVerts.size();
    while (fgets(line, sizeof(line), f)) {
        if (line[0] != 'f' || line[1] != ' ') continue;
        int v[4], t[4], n[4];
        int cnt = 0;
        const char* p = line + 1;
        while (*p && cnt < 4) {
            while (*p == ' ') ++p;
            if (!*p || *p == '\n') break;
            int vi = 0, ti = 0, ni = 0;
            vi = strtol(p, (char**)&p, 10);
            if (*p == '/') { ++p; if (*p != '/') ti = strtol(p, (char**)&p, 10); }
            if (*p == '/') { ++p; ni = strtol(p, (char**)&p, 10); }
            v[cnt] = vi; t[cnt] = ti; n[cnt] = ni; ++cnt;
            while (*p && *p != ' ' && *p != '\n') ++p;
        }
        if (cnt < 3) continue;
        // Fan triangulation (quads supported).
        for (int i = 1; i < cnt - 1; ++i) {
            outIndices.push_back(base + getVertex(v[0], t[0], n[0]));
            outIndices.push_back(base + getVertex(v[i], t[i], n[i]));
            outIndices.push_back(base + getVertex(v[i + 1], t[i + 1], n[i + 1]));
        }
    }
    fclose(f);
    return !outIndices.empty();
}

} // namespace bip
