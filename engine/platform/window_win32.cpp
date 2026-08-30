#include "platform/window_win32.h"

namespace bip {

static const wchar_t* kWndClass = L"BipBipWindowClass";

bool Window::create(const std::string& title, int width, int height) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = &Window::wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512) /* IDC_ARROW */);
    wc.lpszClassName = kWndClass;
    if (!RegisterClassExW(&wc)) return false;

    RECT rc{0, 0, (LONG)width, (LONG)height};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    width_ = width; height_ = height;

    // convert utf8 title -> wide
    int wlen = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nullptr, 0);
    std::wstring wtitle(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, wtitle.data(), wlen);

    hwnd_ = CreateWindowExW(0, kWndClass, wtitle.c_str(), WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            rc.right - rc.left, rc.bottom - rc.top,
                            nullptr, nullptr, wc.hInstance, this);
    if (!hwnd_) return false;

    // raw input: mouse deltas while captured
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;       // generic desktop
    rid.usUsage = 0x02;           // mouse
    rid.dwFlags = 0;
    rid.hwndTarget = hwnd_;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));

    ShowWindow(hwnd_, SW_SHOW);
    return true;
}

LRESULT CALLBACK Window::wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Window* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = (Window*)((CREATESTRUCT*)lp)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (Window*)GetWindowLongPtrW(h, GWLP_USERDATA);
    }
    if (!self) return DefWindowProcW(h, msg, wp, lp);

    // Editor hook: let ImGui see input first when installed (set by the
    // editor via Window::setUiHook). The game leaves this null.
    if (self->uiHook_) {
        if (self->uiHook_(h, msg, wp, lp)) return true;
    }

    switch (msg) {
    case WM_CLOSE:
        self->shouldClose_ = true;
        return 0;
    case WM_SIZE:
        self->width_ = LOWORD(lp);
        self->height_ = HIWORD(lp);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (self->pumpingInput_) self->pumpingInput_->keys[wp & 0xFF] = true;
        if (msg == WM_SYSKEYDOWN && wp == VK_F4) return DefWindowProcW(h, msg, wp, lp);
        return 0;
    case WM_CHAR:
        if (self->pumpingInput_ && self->pumpingInput_->textCount < sizeof(self->pumpingInput_->text))
            self->pumpingInput_->text[self->pumpingInput_->textCount++] = (char)wp;
        return 0;
    case WM_MOUSEWHEEL:
        if (self->pumpingInput_)
            self->pumpingInput_->mouseWheel += (int16_t)HIWORD(wp) / 120; // one notch = 120
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (self->pumpingInput_) self->pumpingInput_->keys[wp & 0xFF] = false;
        return 0;
    case WM_LBUTTONDOWN: if (self->pumpingInput_) self->pumpingInput_->mouseButtons[0] = true; return 0;
    case WM_LBUTTONUP:   if (self->pumpingInput_) self->pumpingInput_->mouseButtons[0] = false; return 0;
    case WM_RBUTTONDOWN: if (self->pumpingInput_) self->pumpingInput_->mouseButtons[1] = true; return 0;
    case WM_RBUTTONUP:   if (self->pumpingInput_) self->pumpingInput_->mouseButtons[1] = false; return 0;
    case WM_MBUTTONDOWN: if (self->pumpingInput_) self->pumpingInput_->mouseButtons[2] = true; return 0;
    case WM_MBUTTONUP:   if (self->pumpingInput_) self->pumpingInput_->mouseButtons[2] = false; return 0;
    case WM_INPUT: {
        if (!self->pumpingInput_) return 0;
        UINT size = sizeof(RAWINPUT);
        static BYTE buf[sizeof(RAWINPUT) * 4];
        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
            return 0;
        auto* raw = (RAWINPUT*)buf;
        if (raw->header.dwType == RIM_TYPEMOUSE &&
            (raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
            self->pumpingInput_->mouseDX += (int16_t)raw->data.mouse.lLastX;
            self->pumpingInput_->mouseDY += (int16_t)raw->data.mouse.lLastY;
        }
        return 0;
    }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void Window::pumpMessages(InputState& io_input) {
    pumpingInput_ = &io_input;
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) shouldClose_ = true;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    pumpingInput_ = nullptr;
}

} // namespace bip
