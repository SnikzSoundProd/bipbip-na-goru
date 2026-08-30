#pragma once
// bipbip editor: Play-In-Editor (PIE) world, Unreal-style.
// The editor owns the AUTHORED scene. Pressing Play builds a separate
// runtime world from a COPY of that scene; simulated state (player,
// velocities, stamina) lives only in the PIE world and is destroyed on Stop.
// This is why editing in Play mode never corrupts the saved level — a real
// Unreal problem we fix by snapshotting at Play time.
#include "core/scene.h"
#include "world/heightfield.h"
#include "physics/verlet.h"
#include "game/player/climber.h"
#include "game/world/route.h"
#include "game/gameplay/run.h"

namespace bip {

enum class EditorMode { Edit, Play };

// A live, simulated instance of a scene.
class PIEWorld {
public:
    bool start(const Scene& authored, ID3D11Device* device);
    void stop();
    void step(float dt);            // one fixed physics step
    void rebuildFromScene(const Scene& s);  // hot-apply scene edits mid-play

    bool active() const { return active_; }

    HeightField   hf;
    VerletWorld   phys;
    VerletWorld   physBuddy;
    Climber       player;
    Climber       buddy;
    bool          buddyActive = false;
    Route         route;
    RunState      run;

    // scene copy this PIE instance was launched from (Play-time snapshot)
    Scene         snapshot;

private:
    bool active_ = false;
    void applyScene(const Scene& s);
};

} // namespace bip
