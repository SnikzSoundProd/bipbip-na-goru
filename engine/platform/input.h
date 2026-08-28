#pragma once
// bipbip input: keyboard + mouse state, filled by Window::pumpMessages
#include <cstdint>

namespace bip {

struct InputState {
    bool keys[256] = {};
    bool mouseButtons[3] = {};   // L, R, M
    int32_t mouseDX = 0;         // raw deltas accumulated this frame
    int32_t mouseDY = 0;
    char text[32] = {};           // WM_CHAR events accumulated this frame
    uint8_t textCount = 0;

    void endFrame() { mouseDX = 0; mouseDY = 0; textCount = 0; }

    bool down(uint8_t k) const { return keys[k]; }
    bool pressed(bool prev, uint8_t k) const { return keys[k] && !prev; } // caller keeps prev snapshot
};

} // namespace bip
