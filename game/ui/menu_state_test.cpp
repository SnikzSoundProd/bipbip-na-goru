#include "game/ui/menu_state.h"
#include <cassert>

using namespace bip;

int main() {
    assert(ConnectionMenu::validIPv4("127.0.0.1"));
    assert(ConnectionMenu::validIPv4("192.168.1.255"));
    assert(!ConnectionMenu::validIPv4(""));
    assert(!ConnectionMenu::validIPv4("127.0.0"));
    assert(!ConnectionMenu::validIPv4("127.0.0.1.2"));
    assert(!ConnectionMenu::validIPv4("127.0.0.256"));
    assert(!ConnectionMenu::validIPv4("127..0.1"));
    assert(!ConnectionMenu::validIPv4("abc.def.0.1"));
    assert(!ConnectionMenu::validIPv4("01.2.3.4"));

    ConnectionMenu m;
    StartMode mode{};
    m.selected = 2;
    assert(!m.activate(&mode));
    assert(m.state == MenuState::Join);
    m.ip = "999.1.1.1";
    assert(!m.activate(&mode));
    assert(m.state == MenuState::Error);
    m.back();
    assert(m.state == MenuState::Join);
    m.ip = "10.0.0.2";
    assert(m.activate(&mode));
    assert(mode == StartMode::Join && m.state == MenuState::Playing);
}
