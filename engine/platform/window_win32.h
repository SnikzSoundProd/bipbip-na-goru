#pragma once
// bipbip platform: Win32 window + raw input pump
#include <windows.h>
#include <string>
#include "platform/input.h"

namespace bip {

class Window {
public:
    bool create(const std::string& title, int width, int height);
    void pumpMessages(InputState& io_input);

    bool shouldClose() const { return shouldClose_; }
    HWND handle() const { return hwnd_; }
    int width() const { return width_; }
    int height() const { return height_; }

    // Editor UI hook: installed by the editor so ImGui receives raw Win32
    // messages before the game input pump. Returns true if handled.
    using UiHook = bool(*)(HWND, UINT, WPARAM, LPARAM);
    void setUiHook(UiHook h) { uiHook_ = h; }

    // Pointer capture for play-in-editor: hide the cursor and keep receiving
    // movement even when it leaves the window.
    void setRawMouseMode(bool enable) {
        if (!hwnd_) return;
        rawMouse_ = enable;
        if (enable) {
            // RAWINPUT already delivers deltas; just hide the OS cursor and
            // clip it so it does not wander onto other monitors.
            ShowCursor(FALSE);
            RECT r; GetClientRect(hwnd_, &r);
            ClientToScreen(hwnd_, reinterpret_cast<POINT*>(&r.left));
            ClientToScreen(hwnd_, reinterpret_cast<POINT*>(&r.right));
            ClipCursor(&r);
        } else {
            ShowCursor(TRUE);
            ClipCursor(nullptr);
        }
    }
    bool rawMouseMode() const { return rawMouse_; }

private:
    friend LRESULT CALLBACK wndProcDispatch(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);

    HWND hwnd_ = nullptr;
    int width_ = 0, height_ = 0;
    bool shouldClose_ = false;
    InputState* pumpingInput_ = nullptr; // valid only during pumpMessages
    UiHook uiHook_ = nullptr;
    bool rawMouse_ = false;
};

} // namespace bip
