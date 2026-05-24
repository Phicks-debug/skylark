// ANSI color terminal output implementation

#include "terminal.hpp"
#include <iostream>

namespace terminal {

std::string_view ansi(Color c) {
    switch (c) {
    case Color::Reset:        return "\033[0m";
    case Color::Black:        return "\033[30m";
    case Color::Red:          return "\033[31m";
    case Color::Green:        return "\033[32m";
    case Color::Yellow:       return "\033[33m";
    case Color::Blue:         return "\033[34m";
    case Color::Magenta:      return "\033[35m";
    case Color::Cyan:         return "\033[36m";
    case Color::White:        return "\033[37m";
    case Color::BoldBlack:    return "\033[1;30m";
    case Color::BoldRed:      return "\033[1;31m";
    case Color::BoldGreen:    return "\033[1;32m";
    case Color::BoldYellow:   return "\033[1;33m";
    case Color::BoldBlue:     return "\033[1;34m";
    case Color::BoldMagenta:  return "\033[1;35m";
    case Color::BoldCyan:     return "\033[1;36m";
    case Color::BoldWhite:    return "\033[1;37m";
    case Color::BrightBlack:  return "\033[90m";
    case Color::BrightRed:    return "\033[91m";
    case Color::BrightGreen:  return "\033[92m";
    case Color::BrightYellow: return "\033[93m";
    case Color::BrightBlue:   return "\033[94m";
    case Color::BrightMagenta: return "\033[95m";
    case Color::BrightCyan:   return "\033[96m";
    case Color::BrightWhite:  return "\033[97m";
    case Color::Dim:          return "\033[2m";
    }
    return "\033[0m";
}

void cprint(std::string_view text, Color color) {
    std::cout << ansi(color) << text << ansi(Color::Reset);
    std::cout.flush();
}

void cprintln(std::string_view text, Color color) {
    cprint(text, color);
    std::cout << '\n';
}

void print_separator(Color color) {
    cprintln("─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─", color);
}

std::string trunc(std::string_view str, size_t max_width) {
    if (str.length() <= max_width) {
        return std::string(str);
    }
    return std::string(str.substr(0, max_width - 3)) + "...";
}

} // namespace terminal
