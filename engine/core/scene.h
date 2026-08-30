#pragma once
// bipbip scene: the AUTHORED, SERIALIZABLE description of a level.
// This is the "editor side" data — what you place and save.
// Runtime state (player position, stamina, physics velocities) lives
// elsewhere and is rebuilt from the scene when Play is pressed (PIE),
// exactly like Unreal's Editor World vs PIE World split.
#include <vector>
#include <string>
#include <cstdint>

namespace bip {

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

    std::vector<SceneBox>  boxes;
    std::vector<SceneHold> holds;

    // spawn point for the local player
    float spawn[3] = {0, 0, 118.f};
};

// ---- (de)serialization to a plain-text .bipscene file ----
// Format is intentionally simple/robust: one directive per line.
bool saveScene(const Scene& s, const std::string& path);
bool loadScene(const std::string& path, Scene& out);

} // namespace bip
