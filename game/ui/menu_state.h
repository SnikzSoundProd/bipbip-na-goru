#pragma once
#include <string>

namespace bip {

enum class MenuState { Main, Join, Error, Playing, Quit };
enum class StartMode { Solo, Host, Join };

struct ConnectionMenu {
    MenuState state = MenuState::Main;
    int selected = 0; // 0 solo, 1 host, 2 join, 3 quit
    std::string ip = "127.0.0.1";
    std::string error;

    static bool validIPv4(const std::string& text);
    bool activate(StartMode* outMode);
    void beginJoin();
    void back();
    void appendChar(char c);
    void eraseChar();
};

} // namespace bip
