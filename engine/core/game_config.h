#pragma once
// bipbip: single source of truth for tunable values.
// Previously these were magic numbers scattered through main.cpp, which meant
// the editor could not expose them and the game/editor could drift apart.
// Defaults here match the game's existing behaviour exactly — loading this
// config must not change how the game plays.
#include <cstdint>
#include <string>

namespace bip {

struct CameraConfig {
    float distance   = 6.5f;    // chase distance behind the player
    float height     = 1.6f;    // vertical offset of the look target
    float pitch      = 0.32f;   // ~18 degrees down
    float fovY       = 1.05f;
    float nearZ      = 0.1f;
    float farZ       = 2000.f;
    float sensitivity = 0.003f; // radians per pixel of mouse movement
    float pitchMin   = -1.2f;
    float pitchMax   =  1.35f;
};

struct PhysicsConfig {
    double fixedDt      = 1.0 / 60.0;
    int    maxSubSteps  = 4;        // guard against death spirals
    float  sleepLinear  = 0.30f;    // below this speed a resting body may sleep
    float  sleepAngular = 0.40f;
    float  sleepDelay   = 0.35f;    // seconds of stillness before sleeping
};

struct GameplayConfig {
    float grabReach     = 2.2f;     // how close a hold must be to grab
    float fallDistance  = 14.f;     // fall this far below last safe spot = fell
    float grabStamina   = 2.f;      // stamina cost per grab
    int   staminaSegs   = 20;       // HUD bar segments
};

struct NetworkConfig {
    uint16_t port        = 27015;
    double   sendRate    = 1.0 / 20.0;   // 20 Hz input/snapshot
    int      maxSnapshotBoxes = 14;
};

struct HudConfig {
    float x      = 16.f;    // top-left origin of the gameplay HUD
    float y      = 14.f;
    float scale  = 2.f;
    float lineH  = 20.f;    // vertical spacing between HUD lines
};

struct WorldConfig {
    uint64_t seed      = 1337;
    float    worldSize = 300.f;
    int      heightN   = 256;
};

struct GameConfig {
    CameraConfig   camera;
    PhysicsConfig  physics;
    GameplayConfig gameplay;
    NetworkConfig  network;
    HudConfig      hud;
    WorldConfig    world;

    // ---- (de)serialization ----
    // Plain-text key = value lines; unknown keys are ignored so older/newer
    // config files stay compatible.
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    // Fill in defaults (used when no file exists).
    void reset();
};

} // namespace bip
