#pragma once
// bipbip scene: the AUTHORED, SERIALIZABLE description of a level.
//
// Two representations, deliberately kept side by side:
//
//   entities[] — the source of truth. A generic list of typed objects
//                (box / hold / mesh / <anything a future game adds>). A
//                second game can use this directly and never touch boxes/holds.
//
//   boxes[] / holds[] — a narrow VIEW for the current game and the editor.
//                Rebuilt from entities by syncView(), materialised into
//                entities by syncDerived(). Keeping the view means the 40
//                existing call sites (editor, PIE, route, physics) did not
//                have to change, while new code can be entity-based.
//
// Runtime state (player position, stamina, physics velocities) lives elsewhere
// and is rebuilt from the scene when Play is pressed (PIE), exactly like
// Unreal's Editor World vs PIE World split.
#include <vector>
#include <string>
#include <cstdint>
#include <optional>

namespace bip {

// ---- entity types --------------------------------------------------------
// Built-in. A new game adds its own constant + component struct without
// forking the engine.
enum EntityType : uint32_t {
    kTypeNone = 0,
    kTypeBox  = 1,
    kTypeHold = 2,
    kTypeMesh = 3,           // renders a .obj from assets/meshes
    // --- reserved for the second game ---
    kTypeLazer        = 100,
    kTypeQuantumPylon = 101,  // also used to prove unknown-type tolerance
};

const char* entityTypeName(uint32_t t);

// ---- components ---------------------------------------------------------
// Optional data attached to an entity. Only the ones matching `type` are
// populated.
struct BoxComp {
    float half[3] = {0.5f, 0.5f, 0.5f};
};

struct HoldComp {
    bool checkpoint = false;
};

struct MeshComp {
    std::string path;            // relative to assets/, e.g. "meshes/climber.obj"
    float tint[4] = {1.f, 1.f, 1.f, 1.f};
    bool  castShadow = true;
};

struct LazerComp {
    float color[3] = {1.f, 0.2f, 0.2f};
    int   damage = 1;
};

// ---- entity -------------------------------------------------------------
struct Entity {
    uint32_t id = 0;
    uint32_t type = kTypeNone;

    float pos[3]    = {0.f, 0.f, 0.f};
    float rotDeg[3] = {0.f, 0.f, 0.f};
    float scale[3]  = {1.f, 1.f, 1.f};
    int   owner = 0;              // 0=host, 1=client (ownership hint)

    std::optional<BoxComp>   box;
    std::optional<HoldComp>  hold;
    std::optional<MeshComp>  mesh;
    std::optional<LazerComp> lazer;

    bool visible = true;
    std::string tag;              // free-form, for editor grouping

    void clearComponents() { box.reset(); hold.reset(); mesh.reset(); lazer.reset(); }
};

// ---- legacy narrow types -------------------------------------------------
// Kept: 40 call sites in the editor, PIE and the route builder speak these.
struct SceneBox {
    float pos[3] = {0, 0, 0};
    float half[3] = {0.5f, 0.5f, 0.5f};
    // yaw/pitch/roll in degrees for easy authoring; converted to quaternion
    float rotDeg[3] = {0, 0, 0};
    int   owner = 0;              // 0=host, 1=client (ownership hint)
};

struct SceneHold {
    float pos[3] = {0, 0, 0};
    bool  checkpoint = false;
};

struct Scene {
    uint64_t seed = 1337;          // procedural mountain seed (shared by peers)
    float    worldSize = 220.f;
    int      heightN = 256;

    // source of truth
    std::vector<Entity> entities;

    // narrow view for the current game / editor
    std::vector<SceneBox>  boxes;
    std::vector<SceneHold> holds;

    // spawn point for the local player
    float spawn[3] = {0, 0, 118.f};

    // --- view <-> entities ---
    // syncView():     entities -> boxes/holds   (call after editing entities)
    // syncDerived():  boxes/holds -> entities   (call after editing the view)
    // They are each other's inverse for the types the view understands;
    // entity types the view does not model (mesh, lazer, ...) are preserved.
    void syncView();
    void syncDerived();

    uint32_t nextFreeId() const;
    size_t   countByType(uint32_t t) const;
    Entity*  findEntity(uint32_t id);
};

size_t countByType(const Scene& s, uint32_t t);

// ---- (de)serialization ---------------------------------------------------
// v1 (legacy, positional): the original .bipscene format. Still written by
// saveScene() so the current game and its scenes are untouched.
bool saveScene(const Scene& s, const std::string& path);
bool loadScene(const std::string& path, Scene& out);

// v2 (entity format, key=value, versioned, forward-compatible):
//   bipscene 2
//   seed 1337
//   world 300 256
//   spawn 0 0 118
//   entity 1 box pos=1 2 3 half=0.5 0.5 0.5 rot=0 90 0 owner=0
//   entity 2 hold pos=4 5 6 checkpoint=1
//   entity 3 quantumpylon pos=1 1 1 wobble=0.77        <- unknown keys are kept
// Unknown directives and unknown keys are preserved/ignored rather than
// rejected, so a scene written by a newer build still opens.
bool saveSceneV2(const Scene& s, const std::string& path);
bool loadSceneV2(const std::string& path, Scene& out);

} // namespace bip
