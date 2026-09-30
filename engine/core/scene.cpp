#include "core/scene.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace bip {

// ------------------------------------------------------------------ types
const char* entityTypeName(uint32_t t) {
    switch (t) {
        case kTypeNone: return "none";
        case kTypeBox:  return "box";
        case kTypeHold: return "hold";
        case kTypeMesh: return "mesh";
        case kTypeLazer: return "lazer";
        case kTypeQuantumPylon: return "quantumpylon";
        default: return "unknown";
    }
}

// entityTypeFromName is deliberately open-ended: an unknown name maps to a
// stable pseudo-id so a scene written by a future build still round-trips its
// entities instead of dropping them.
static uint32_t entityTypeFromName(const std::string& n) {
    if (n == "box")  return kTypeBox;
    if (n == "hold") return kTypeHold;
    if (n == "mesh") return kTypeMesh;
    if (n == "lazer") return kTypeLazer;
    if (n == "quantumpylon") return kTypeQuantumPylon;
    if (n == "none") return kTypeNone;
    return 1000u;   // unregistered
}

// ------------------------------------------------------- view <-> entities
void Scene::syncDerived() {
    // boxes/holds -> entities. Keep entity types the view does not model
    // (mesh, lazer, unknown) so a second game's content is never destroyed by
    // the current game touching the scene.
    std::vector<Entity> keep;
    keep.reserve(entities.size() + boxes.size() + holds.size());
    for (auto& e : entities)
        if (e.type != kTypeBox && e.type != kTypeHold) keep.push_back(e);

    for (const auto& b : boxes) {
        Entity e;
        e.type = kTypeBox;
        for (int k = 0; k < 3; ++k) { e.pos[k] = b.pos[k]; e.rotDeg[k] = b.rotDeg[k]; }
        e.owner = b.owner;
        BoxComp bc;
        for (int k = 0; k < 3; ++k) bc.half[k] = b.half[k];
        e.box = bc;
        keep.push_back(e);
    }
    for (const auto& h : holds) {
        Entity e;
        e.type = kTypeHold;
        for (int k = 0; k < 3; ++k) e.pos[k] = h.pos[k];
        HoldComp hc; hc.checkpoint = h.checkpoint;
        e.hold = hc;
        keep.push_back(e);
    }
    entities = std::move(keep);
    // assign ids now that the full list is known
    uint32_t next = 1;
    std::vector<bool> used(entities.size(), false);
    for (auto& e : entities) if (e.id) used[e.id % entities.size()] = true;
    for (auto& e : entities) {
        if (e.id == 0) {
            while (next < used.size() && used[next]) ++next;
            e.id = next;
        }
        if (e.id < used.size()) used[e.id] = true;
    }
}

void Scene::syncView() {
    // entities -> boxes/holds. Entities of other types simply do not appear
    // in the narrow view.
    boxes.clear();
    holds.clear();
    for (const auto& e : entities) {
        if (e.type == kTypeBox) {
            SceneBox b;
            for (int k = 0; k < 3; ++k) { b.pos[k] = e.pos[k]; b.rotDeg[k] = e.rotDeg[k]; }
            b.owner = e.owner;
            if (e.box) for (int k = 0; k < 3; ++k) b.half[k] = e.box->half[k];
            boxes.push_back(b);
        } else if (e.type == kTypeHold) {
            SceneHold h;
            for (int k = 0; k < 3; ++k) h.pos[k] = e.pos[k];
            h.checkpoint = e.hold ? e.hold->checkpoint : false;
            holds.push_back(h);
        }
    }
}

uint32_t Scene::nextFreeId() const {
    uint32_t mx = 0;
    for (const auto& e : entities) mx = std::max(mx, e.id);
    return mx + 1;
}

size_t Scene::countByType(uint32_t t) const {
    size_t n = 0;
    for (const auto& e : entities) if (e.type == t) ++n;
    return n;
}

Entity* Scene::findEntity(uint32_t id) {
    for (auto& e : entities) if (e.id == id) return &e;
    return nullptr;
}

size_t countByType(const Scene& s, uint32_t t) { return s.countByType(t); }

// ------------------------------------------------- v1 (legacy) format
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

// ------------------------------------------------- v2 (entity) format
static bool parseTriplet(const char* v, float* out) {
    return sscanf(v, "%f %f %f", &out[0], &out[1], &out[2]) == 3;
}

// Splits "key=value" pairs out of an entity line. The VALUE runs until the next
// "<ident>=" token, so unquoted multi-component values like
//   pos=1 2 3 rot=0 90 0 owner=1
// parse correctly. A naive "%255s" would stop at the first space, capture only
// "1", and leave "2 3 rot=..." as garbage that desynchronises the rest of the
// line.
static bool isIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static bool nextKV(const char*& p, char* key, int keyCap, char* val, int valCap) {
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p) return false;

    int i = 0;
    while (*p && *p != '=' && i < keyCap - 1) {
        if (!isIdentChar(*p)) return false;
        key[i++] = *p++;
    }
    key[i] = 0;
    if (*p != '=') return false;
    ++p;

    const char* start = p;
    const char* q = p;
    while (*q) {
        if (*q == ' ') {
            const char* r = q + 1;
            int j = 0;
            while (*r && isIdentChar(*r)) { ++r; ++j; }
            if (j > 0 && *r == '=') break;      // next key starts here
        }
        ++q;
    }
    int len = (int)(q - start);
    while (len > 0 && start[len - 1] == ' ') --len;   // trim trailing spaces
    if (len > valCap - 1) len = valCap - 1;
    memcpy(val, start, (size_t)len);
    val[len] = 0;
    p = q;
    return true;
}

bool saveSceneV2(const Scene& s, const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fprintf(f, "bipscene 2\n");
    fprintf(f, "seed %llu\n", (unsigned long long)s.seed);
    fprintf(f, "world %g %d\n", (double)s.worldSize, s.heightN);
    fprintf(f, "spawn %g %g %g\n",
            (double)s.spawn[0], (double)s.spawn[1], (double)s.spawn[2]);
    for (const auto& e : s.entities) {
        fprintf(f, "entity %u %s pos=%g %g %g rot=%g %g %g scale=%g %g %g owner=%d",
                e.id, entityTypeName(e.type),
                (double)e.pos[0], (double)e.pos[1], (double)e.pos[2],
                (double)e.rotDeg[0], (double)e.rotDeg[1], (double)e.rotDeg[2],
                (double)e.scale[0], (double)e.scale[1], (double)e.scale[2],
                e.owner);
        if (e.box)
            fprintf(f, " half=%g %g %g",
                    (double)e.box->half[0], (double)e.box->half[1], (double)e.box->half[2]);
        if (e.hold) fprintf(f, " checkpoint=%d", e.hold->checkpoint ? 1 : 0);
        if (e.mesh) {
            fprintf(f, " mesh=%s", e.mesh->path.c_str());
            fprintf(f, " tint=%g %g %g %g",
                    (double)e.mesh->tint[0], (double)e.mesh->tint[1],
                    (double)e.mesh->tint[2], (double)e.mesh->tint[3]);
            fprintf(f, " castShadow=%d", e.mesh->castShadow ? 1 : 0);
        }
        if (e.lazer) {
            fprintf(f, " color=%g %g %g",
                    (double)e.lazer->color[0], (double)e.lazer->color[1],
                    (double)e.lazer->color[2]);
            fprintf(f, " damage=%d", e.lazer->damage);
        }
        if (!e.tag.empty()) fprintf(f, " tag=%s", e.tag.c_str());
        fprintf(f, " visible=%d\n", e.visible ? 1 : 0);
    }
    fclose(f);
    return true;
}

bool loadSceneV2(const std::string& path, Scene& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out = Scene{};

    char line[1024];
    bool sawHeader = false;
    bool sawEntity = false;      // v2 marker; a v1 file has "box "/"hold " lines
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
        if (!n || line[0] == '#') continue;

        if (!strncmp(line, "bipscene ", 9)) {
            int v = 0;
            sscanf(line + 9, "%d", &v);
            if (v < 2) {   // explicitly an older version
                fclose(f);
                Scene tmp;
                if (!loadScene(path, tmp)) return false;
                out = tmp;
                out.syncDerived();   // upgrade to entities
                return true;
            }
            sawHeader = true;
        } else if (!strncmp(line, "box ", 4) || !strncmp(line, "hold ", 5)) {
            // A v1 file (no "bipscene" header, positional box/hold lines).
            // Re-read it with the legacy parser and upgrade to entities.
            fclose(f);
            Scene tmp;
            if (!loadScene(path, tmp)) return false;
            out = tmp;
            out.syncDerived();
            return true;
        } else if (!strncmp(line, "seed ", 5)) {
            unsigned long long v = 0;
            if (sscanf(line + 5, "%llu", &v) == 1) out.seed = v;
        } else if (!strncmp(line, "world ", 6)) {
            float sz = 220.f; int nn = 256;
            if (sscanf(line + 6, "%f %d", &sz, &nn) == 2) { out.worldSize = sz; out.heightN = nn; }
        } else if (!strncmp(line, "spawn ", 6)) {
            float a = 0, b = 0, c = 0;
            if (sscanf(line + 6, "%f %f %f", &a, &b, &c) == 3) {
                out.spawn[0] = a; out.spawn[1] = b; out.spawn[2] = c;
            }
        } else if (!strncmp(line, "entity ", 7)) {
            Entity e;
            char typeName[64] = {0};
            if (sscanf(line + 7, "%u %63s", &e.id, typeName) != 2) continue;
            e.type = entityTypeFromName(typeName);

            // Skip "<id> <type> " — the key scan must start AFTER the type,
            // otherwise "box" is read as a key and every following pair is lost.
            const char* p = line + 7;
            while (*p == ' ') ++p;
            while (*p && *p != ' ') ++p;      // id
            while (*p == ' ') ++p;
            while (*p && *p != ' ') ++p;      // type

            char key[64] = {0}, val[256] = {0};
            while (nextKV(p, key, 64, val, 256)) {
                if (!strcmp(key, "pos"))       parseTriplet(val, e.pos);
                else if (!strcmp(key, "rot"))  parseTriplet(val, e.rotDeg);
                else if (!strcmp(key, "scale"))parseTriplet(val, e.scale);
                else if (!strcmp(key, "owner")) e.owner = atoi(val);
                else if (!strcmp(key, "visible")) e.visible = (atoi(val) != 0);
                else if (!strcmp(key, "tag"))   e.tag = val;
                else if (!strcmp(key, "half")) {
                    BoxComp bc; if (parseTriplet(val, bc.half)) e.box = bc;
                } else if (!strcmp(key, "checkpoint")) {
                    HoldComp hc; hc.checkpoint = (atoi(val) != 0); e.hold = hc;
                } else if (!strcmp(key, "mesh")) {
                    if (!e.mesh) e.mesh = MeshComp{};
                    e.mesh->path = val;
                } else if (!strcmp(key, "tint")) {
                    float t4[4] = {1,1,1,1};
                    sscanf(val, "%f %f %f %f", &t4[0], &t4[1], &t4[2], &t4[3]);
                    if (!e.mesh) e.mesh = MeshComp{};
                    for (int k = 0; k < 4; ++k) e.mesh->tint[k] = t4[k];
                } else if (!strcmp(key, "castShadow")) {
                    if (!e.mesh) e.mesh = MeshComp{};
                    e.mesh->castShadow = (atoi(val) != 0);
                } else if (!strcmp(key, "color")) {
                    LazerComp lz;
                    if (parseTriplet(val, lz.color)) e.lazer = lz;
                } else if (!strcmp(key, "damage")) {
                    if (!e.lazer) e.lazer = LazerComp{};
                    e.lazer->damage = atoi(val);
                }
                // unknown keys are intentionally ignored -> forward compatible
            }
            sawEntity = true;
            out.entities.push_back(e);
        }
        // unknown directives are ignored -> forward compatible
    }
    fclose(f);

    // Keep the narrow view in sync so old code reading boxes/holds still works
    // on a v2 scene.
    out.syncView();
    (void)sawHeader; (void)sawEntity;
    return true;
}

} // namespace bip
