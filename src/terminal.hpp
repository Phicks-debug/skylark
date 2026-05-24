// Terminal output utilities with ANSI color support
// Mirrors the Python TerminalOutput class

#ifndef TERMINAL_HPP
#define TERMINAL_HPP

#include <string>
#include <string_view>

namespace terminal {

// ANSI color codes
enum class Color {
    Reset,
    Black, Red, Green, Yellow, Blue, Magenta, Cyan, White,
    BoldBlack, BoldRed, BoldGreen, BoldYellow,
    BoldBlue, BoldMagenta, BoldCyan, BoldWhite,
    BrightBlack, BrightRed, BrightGreen, BrightYellow,
    BrightBlue, BrightMagenta, BrightCyan, BrightWhite,
    Dim,
};

// Get the ANSI escape code for a color
std::string_view ansi(Color c);

// Print colored text to stdout (no newline, auto-flush)
void cprint(std::string_view text, Color color = Color::Reset);

// Print colored text with a trailing newline
void cprintln(std::string_view text, Color color = Color::Reset);

// Print a separator line
void print_separator(Color color = Color::Reset);

// Truncate a string to fit within a given width, adding "..." if truncated
std::string trunc(std::string_view str, size_t max_width);

} // namespace terminal

#endif // TERMINAL_HPP
