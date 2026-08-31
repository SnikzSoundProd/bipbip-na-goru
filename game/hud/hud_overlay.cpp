#include "hud_overlay.h"
#include <cstdio>
#include <string>
#include <cmath>
#include <algorithm>

namespace bip {

// Mirrors the HUD that used to be inline in main.cpp, so the editor shows the
// exact same overlay the game does.
void drawGameplayHud(TextRenderer& hud, const HudInputs& in, const GameConfig& cfg) {
    if (!in.run || !in.player) return;

    const float x  = cfg.hud.x;
    const float y0 = cfg.hud.y;
    const float sc = cfg.hud.scale;
    const float lh = cfg.hud.lineH;

    hud.draw("STAMINA", x, y0, sc, 1, 1, 1);
    {
        int filled = (int)(in.run->stamina / 100.f * cfg.gameplay.staminaSegs + 0.5f);
        std::string bar;
        for (int i = 0; i < cfg.gameplay.staminaSegs; ++i) bar += (i < filled) ? '#' : '.';
        float cr = in.run->stamina > 40 ? 0.2f : 1.0f;
        float cg = in.run->stamina > 40 ? 0.85f : 0.25f;
        hud.draw(bar, x, y0 + lh, sc, cr, cg, 0.2f);
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "TIME %.1f  FALLS %d", in.run->runTime, in.run->falls);
    hud.draw(buf, x, y0 + lh * 2.f, sc, 1, 1, 1);

    int nearHold = in.route ? in.route->nearest(in.player->pelvisPos(),
                                                cfg.gameplay.grabReach) : -1;
    snprintf(buf, sizeof(buf), "HOLDS NEAR: %s   SEED %llu",
             nearHold >= 0 ? "GRAB!" : "-", (unsigned long long)in.seed);
    hud.draw(buf, x, y0 + lh * 3.f, sc, 0.85f, 0.85f, 0.9f);

    // net status line
    const char* netStatus = in.isSolo ? "SOLO" :
        (in.connected ? (in.isHost ? "HOST: peer connected" : "JOINED") :
         (in.isHost ? "HOST: waiting..." : "JOINING..."));
    hud.draw(netStatus, x, y0 + lh * 4.f, sc,
             in.connected || in.isSolo ? 0.4f : 1.f,
             in.isSolo ? 0.7f : (in.connected ? 1.f : 0.4f), 0.4f);

    if (!in.isSolo) {
        snprintf(buf, sizeof(buf), "BUDDY: %s", in.buddyActive ? "ON" : "off");
        hud.draw(buf, x, y0 + lh * 5.f, sc,
                 in.buddyActive ? 0.3f : 0.7f,
                 in.buddyActive ? 1.f : 0.3f, 0.4f);
    }

    if (in.run->exhausted)
        hud.draw("HANDS SLIP! REST!", x + 460.f, y0 + 46.f, sc + 1.f, 1, 0.25f, 0.2f);

    if (in.run->finished) {
        hud.draw("SUMMIT!!!", x + 454.f, y0 + 226.f, sc + 6.f, 1, 0.85f, 0.1f);
        snprintf(buf, sizeof(buf), "TIME %.1fs   FALLS %d", in.run->finishTime, in.run->falls);
        hud.draw(buf, x + 484.f, y0 + 316.f, sc + 1.f, 1, 1, 1);
        hud.draw("ESC TO QUIT - R FOR NEW RUN", x + 414.f, y0 + 366.f, sc, 0.9f, 0.9f, 0.9f);
    }
}

// F3 profiler: fps / frame time / draw calls / sleeping bodies + net graph.
void drawProfilerOverlay(TextRenderer& hud, const HudInputs& in,
                         float screenW, float screenH) {
    char pb[256];
    snprintf(pb, sizeof(pb),
             "FPS %.0f   FRAME %.2f ms   DRAW CALLS %d   TRIS ~%d   SLEEP %d/%d",
             in.fps, in.frameMs, in.drawCalls, in.drawCalls * 36,
             in.boxesSleep, in.boxesTotal);
    hud.draw(pb, 16.f, screenH - 90.f, 2.f, 0.6f, 1.f, 0.6f);

    if (in.net) {
        char nb[256];
        snprintf(nb, sizeof(nb),
                 "NET  sent %u/s  recv %u/s   (%llu sent / %llu recv)",
                 in.net->sentPerSec, in.net->recvPerSec,
                 (unsigned long long)in.net->packetsSent,
                 (unsigned long long)in.net->packetsRecv);
        hud.draw(nb, 16.f, screenH - 70.f, 2.f, 0.6f, 0.8f, 1.f);

        // simple bars: sent (green) above recv (cyan)
        int bx = 16, by = (int)(screenH - 50.f), bw = 200, bh = 8;
        float sFrac = std::min(1.f, in.net->sentPerSec / 60.f);
        float rFrac = std::min(1.f, in.net->recvPerSec / 60.f);
        (void)screenW;
        for (int i = 0; i < bw; ++i) {
            if (i < (int)(sFrac * bw))
                hud.draw("|", (float)(bx + i), (float)by, 2.f, 0.3f, 1.f, 0.4f);
            if (i < (int)(rFrac * bw))
                hud.draw("|", (float)(bx + i), (float)(by + bh), 2.f, 0.3f, 0.9f, 1.f);
        }
    }
    hud.draw("F3: HIDE", 16.f, screenH - 30.f, 1.5f, 0.7f, 0.7f, 0.7f);
}

} // namespace bip
