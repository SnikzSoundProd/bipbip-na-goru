#pragma once
// bipbip: tunable values, split by WHO OWNS them.
//
// STEP 3 of generalising the engine. Previously one GameConfig mixed
// engine-wide knobs (fixed step, sleep thresholds, send rate, camera) with
// game-specific ones (grab reach, fall distance, stamina). A second game would
// either inherit climbing values it does not understand, or need a fork.
//
//   EngineConfig  — engine-wide. The same for every game: how the simulation is
//                   stepped, how bodies sleep, how often snapshots are sent,
//                   the camera rig, world generation.
//   GameplayConfig — the GAME's own numbers. Swapping this file is what makes
//                   it a different game.
//
// Both accept the legacy combined format, so the shipped game.cfg keeps working
// and existing user tweaks are not lost.
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

// Game-specific tuning. Holds the climbing numbers AND the HUD placement,
// because both belong to "this game" rather than to the engine.
struct GameplayConfig {
    // climbing
    float grabReach     = 2.2f;     // how close a hold must be to grab
    float fallDistance  = 14.f;     // fall this far below last safe spot = fell
    float grabStamina   = 2.f;      // stamina cost per grab
    int   staminaSegs   = 20;       // HUD bar segments

    // HUD placement
    float hudX     = 16.f;    // top-left origin of the gameplay HUD
    float hudY     = 14.f;
    float hudScale = 2.f;
    float hudLineH = 20.f;    // vertical spacing between HUD lines

    // load()/save() also understand the legacy combined keys, so a single
    // game.cfg containing camera./physics./gameplay. lines still fills this.
    bool load(const std::string& path);
    bool save(const std::string& path) const;
    void reset();
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

// ---------------------------------------------------------------------------
// EngineConfig — identical for every game built on this engine.
struct EngineConfig {
    CameraConfig   camera;
    PhysicsConfig  physics;
    NetworkConfig  network;
    WorldConfig    world;

    // load() ignores gameplay.* (and any other unknown) key, so one file can
    // feed both halves. save() never writes gameplay.* keys.
    bool load(const std::string& path);
    bool save(const std::string& path) const;
    void reset();
};

// ---------------------------------------------------------------------------
// Legacy COMBINED config. Kept so the ~40 existing call sites (game, editor)
// and the shipped assets/config/game.cfg keep working unchanged. New code should
// prefer EngineConfig + GameplayConfig.
struct GameConfig {
    CameraConfig   camera;
    PhysicsConfig  physics;
    GameplayConfig gameplay;
    NetworkConfig  network;
    HudConfig      hud;          // legacy view of GameplayConfig's HUD fields
    WorldConfig    world;

    // Plain-text key = value lines; unknown keys are ignored so older/newer
    // config files stay compatible.
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    // Fill in defaults (used when no file exists).
    void reset();

    // Split/merge with the new halves, so a single file can drive both.
    void applyEngine(const EngineConfig& e);
    void applyGameplay(const GameplayConfig& g);
};

} // namespace bip
