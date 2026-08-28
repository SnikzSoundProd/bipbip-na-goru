#include "game/ui/menu_state.h"
#include <cctype>

namespace bip {

bool ConnectionMenu::validIPv4(const std::string& text) {
    if (text.empty()) return false;
    int parts = 0, value = 0, digits = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        char c = i < text.size() ? text[i] : '.';
        if (c >= '0' && c <= '9') {
            if (++digits > 3) return false;
            value = value * 10 + (c - '0');
            continue;
        }
        if (c != '.' || digits == 0 || value > 255 || (digits > 1 && text[i - digits] == '0'))
            return false;
        if (++parts > 4) return false;
        value = 0; digits = 0;
    }
    return parts == 4;
}

bool ConnectionMenu::activate(StartMode* outMode) {
    if (selected == 0) { if (outMode) *outMode = StartMode::Solo; state = MenuState::Playing; return true; }
    if (selected == 1) { if (outMode) *outMode = StartMode::Host; state = MenuState::Playing; return true; }
    if (selected == 3) { state = MenuState::Quit; return false; }
    if (state == MenuState::Main) { beginJoin(); return false; }
    if (!validIPv4(ip)) { error = "INVALID IPv4 (example: 127.0.0.1)"; state = MenuState::Error; return false; }
    if (outMode) *outMode = StartMode::Join;
    state = MenuState::Playing;
    error.clear();
    return true;
}

void ConnectionMenu::beginJoin() { state = MenuState::Join; error.clear(); }
void ConnectionMenu::back() { if (state == MenuState::Error) state = MenuState::Join; else state = MenuState::Main; error.clear(); }
void ConnectionMenu::appendChar(char c) { if (state == MenuState::Join && ((c >= '0' && c <= '9') || c == '.') && ip.size() < 15) ip += c; }
void ConnectionMenu::eraseChar() { if (state == MenuState::Join && !ip.empty()) ip.pop_back(); }

} // namespace bip
