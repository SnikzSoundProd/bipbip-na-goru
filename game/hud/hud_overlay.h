#pragma once
// bipbip HUD: the in-game overlay AND the F3 profiler, in one shared place.
// Previously this lived inline in main.cpp, so the editor had no HUD and no
// profiler despite the user asking for editor/game parity. Both now call this.
#include "render/text_renderer.h"
#include "core/game_config.h"
#include "game/gameplay/run.h"
#include "game/world/route.h"
#include "game/player/climber.h"
#include "physics/verlet.h"
#include "net/net_layer.h"
#include <cstdint>

namespace bip {

// --- data the HUD needs, gathered by the caller (game or editor) ---
struct HudInputs {
    const RunState*  run        = nullptr;
    const Climber*   player     = nullptr;
    const Route*     route      = nullptr;
    const VerletWorld* phys     = nullptr;
    const NetLayer*  net        = nullptr;
    bool   isSolo     = true;
    bool   buddyActive = false;
    bool   connected  = false;
    bool   isHost     = false;
    uint64_t seed     = 1337;

    // profiler values (0 when unknown)
    float  fps        = 0.f;
    float  frameMs    = 0.f;
    int    drawCalls  = 0;
    int    boxesSleep = 0;
    int    boxesTotal = 0;
};

// Draw the gameplay HUD (stamina, timer, holds, net status, summit screen).
// `cfg` supplies the layout/scale so it can be tuned from the editor.
void drawGameplayHud(TextRenderer& hud, const HudInputs& in, const GameConfig& cfg);

// Draw the F3 profiler overlay (fps, frame ms, draw calls, net graph).
void drawProfilerOverlay(TextRenderer& hud, const HudInputs& in,
                         float screenW, float screenH);

} // namespace bip
