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

private:
    friend LRESULT CALLBACK wndProcDispatch(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);

    HWND hwnd_ = nullptr;
    int width_ = 0, height_ = 0;
    bool shouldClose_ = false;
    InputState* pumpingInput_ = nullptr; // valid only during pumpMessages
    UiHook uiHook_ = nullptr;
};

} // namespace bip
