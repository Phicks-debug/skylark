// Markdown-to-ANSI renderer implementation

#include "markdown_renderer.hpp"
#include <algorithm>
#include <cctype>

namespace markdown {

// ---- ANSI helper codes (kept local to avoid coupling with terminal.hpp) ----
namespace ansi {
    constexpr const char* reset   = "\033[0m";
    constexpr const char* bold    = "\033[1m";
    constexpr const char* dim     = "\033[2m";
    constexpr const char* italic  = "\033[3m";
    constexpr const char* uline   = "\033[4m";
    constexpr const char* strikethrough = "\033[9m";

    // Foreground colors
    constexpr const char* fg_yellow   = "\033[33m";
    constexpr const char* fg_magenta  = "\033[35m";
    constexpr const char* fg_cyan     = "\033[36m";
    constexpr const char* fg_bright_cyan   = "\033[96m";
    constexpr const char* fg_bright_white  = "\033[97m";

    // Box-drawing characters as UTF-8 strings
    constexpr const char* dash = "\xe2\x94\x80";       // ─
    constexpr const char* pipe_block = "\xe2\x96\x8c"; // ▌
    constexpr const char* bullet = "\xe2\x80\xa2";     // •
}

// ---- Helper: apply inline markdown formatting to a line ----
// Handles: **bold**, *italic*, `code`, ~~strikethrough~~, [text](url)
// Does NOT handle block-level constructs (headings, code blocks, lists)
static std::string format_inline(std::string_view text) {
    std::string result;
    result.reserve(text.size() * 2);

    size_t i = 0;
    while (i < text.size()) {
        // Bold: **text** or __text__
        if (i + 1 < text.size() && text[i] == '*' && text[i+1] == '*') {
            size_t end = text.find("**", i + 2);
            if (end != std::string::npos && end > i + 2) {
                result += ansi::bold;
                result += ansi::fg_bright_white;
                result += format_inline(text.substr(i + 2, end - i - 2));
                result += ansi::reset;
                i = end + 2;
                continue;
            }
        }
        if (i + 1 < text.size() && text[i] == '_' && text[i+1] == '_') {
            size_t end = text.find("__", i + 2);
            if (end != std::string::npos && end > i + 2) {
                result += ansi::bold;
                result += ansi::fg_bright_white;
                result += format_inline(text.substr(i + 2, end - i - 2));
                result += ansi::reset;
                i = end + 2;
                continue;
            }
        }

        // Strikethrough: ~~text~~
        if (i + 1 < text.size() && text[i] == '~' && text[i+1] == '~') {
            size_t end = text.find("~~", i + 2);
            if (end != std::string::npos && end > i + 2) {
                result += ansi::strikethrough;
                result += ansi::dim;
                result += format_inline(text.substr(i + 2, end - i - 2));
                result += ansi::reset;
                i = end + 2;
                continue;
            }
        }

        // Inline code: `text`
        if (text[i] == '`') {
            size_t end = text.find('`', i + 1);
            if (end != std::string::npos && end > i + 1) {
                result += ansi::dim;
                result += ansi::fg_bright_cyan;
                result += text.substr(i + 1, end - i - 1);
                result += ansi::reset;
                i = end + 1;
                continue;
            }
        }

        // Italic: *text* (but not ** which is bold)
        // Must be preceded by non-* and followed by non-* to avoid confusion with bold
        if (text[i] == '*' && (i == 0 || text[i-1] != '*') &&
            (i + 2 >= text.size() || text[i+2] != '*')) {
            // Find next single * that is not adjacent to another *
            size_t j = i + 1;
            bool found = false;
            while (j < text.size()) {
                if (text[j] == '*' && (j + 1 >= text.size() || text[j+1] != '*')) {
                    if (j > i + 1) {
                        result += ansi::italic;
                        result += format_inline(text.substr(i + 1, j - i - 1));
                        result += ansi::reset;
                        i = j + 1;
                        found = true;
                        break;
                    }
                }
                j++;
            }
            if (found) continue;
            // No closing * found, output literally
            result += text[i];
            i++;
            continue;
        }

        // Link: [text](url) — show text with URL in dim
        if (text[i] == '[') {
            size_t close_bracket = text.find(']', i + 1);
            if (close_bracket != std::string::npos &&
                close_bracket + 1 < text.size() &&
                text[close_bracket + 1] == '(') {
                size_t close_paren = text.find(')', close_bracket + 2);
                if (close_paren != std::string::npos) {
                    std::string_view link_text = text.substr(i + 1, close_bracket - i - 1);
                    std::string_view link_url = text.substr(close_bracket + 2, close_paren - close_bracket - 2);
                    result += ansi::uline;
                    result += ansi::fg_cyan;
                    result += link_text;
                    result += ansi::reset;
                    result += ansi::dim;
                    result += " (";
                    result += link_url;
                    result += ")";
                    result += ansi::reset;
                    i = close_paren + 1;
                    continue;
                }
            }
        }

        result += text[i];
        i++;
    }

    return result;
}

// Make a horizontal rule (repeated dash chars)
static std::string make_hr(size_t width) {
    std::string s;
    for (size_t i = 0; i < width; i++) {
        s += ansi::dash;
        if (i % 2 == 1) s += ' ';
    }
    return s;
}

// ---- Render a single line of markdown (block-level + inline) ----
std::string render_line(std::string_view line, RenderState& state) {
    // Trim trailing carriage return
    std::string trimmed(line);
    while (!trimmed.empty() && (trimmed.back() == '\r' || trimmed.back() == '\n'))
        trimmed.pop_back();

    // ---- Code block handling ----
    if (trimmed.size() >= 3 && trimmed.substr(0, 3) == "```") {
        if (state.in_code_block) {
            // Closing code block
            state.in_code_block = false;
            state.code_block_lang.clear();
            return std::string(ansi::dim) + std::string(ansi::dash) +
                   std::string(ansi::dash) + std::string(ansi::dash) +
                   " end code " +
                   std::string(ansi::dash) + std::string(ansi::dash) +
                   std::string(ansi::dash) + ansi::reset;
        } else {
            // Opening code block
            state.in_code_block = true;
            std::string lang = trimmed.substr(3);
            auto start = lang.find_first_not_of(" \t");
            if (start != std::string::npos) {
                auto end = lang.find_last_not_of(" \t");
                state.code_block_lang = lang.substr(start, end - start + 1);
            }
            std::string header = std::string(ansi::dim) +
                                 std::string(ansi::dash) +
                                 std::string(ansi::dash) +
                                 std::string(ansi::dash) + " code";
            if (!state.code_block_lang.empty())
                header += " (" + state.code_block_lang + ")";
            header += " " +
                      std::string(ansi::dash) +
                      std::string(ansi::dash) +
                      std::string(ansi::dash) + ansi::reset;
            return header;
        }
    }

    if (state.in_code_block) {
        // Code block content: dimmed, no inline formatting
        return std::string(ansi::dim) + ansi::fg_bright_cyan + trimmed + ansi::reset;
    }

    // Empty line
    if (trimmed.empty())
        return "";

    // ---- Headings ----
    size_t hash_count = 0;
    while (hash_count < trimmed.size() && trimmed[hash_count] == '#')
        hash_count++;

    if (hash_count > 0 && hash_count <= 6 &&
        hash_count < trimmed.size() && trimmed[hash_count] == ' ') {
        std::string_view heading_text = trimmed;
        heading_text.remove_prefix(hash_count + 1);
        while (!heading_text.empty() && heading_text.back() == '#')
            heading_text.remove_suffix(1);
        while (!heading_text.empty() && heading_text.back() == ' ')
            heading_text.remove_suffix(1);

        std::string result;
        if (hash_count == 1) {
            // H1: bordered heading
            std::string border = make_hr(heading_text.size() / 2 + 1);
            result += ansi::bold;
            result += ansi::fg_yellow;
            result += border;
            result += "\n ";
            result += ansi::bold;
            result += ansi::fg_bright_white;
            result += heading_text;
            result += "\n";
            result += ansi::bold;
            result += ansi::fg_yellow;
            result += border;
            result += ansi::reset;
        } else {
            result += ansi::bold;
            result += ansi::fg_yellow;
            result += std::string(hash_count, '#');
            result += " ";
            result += ansi::fg_bright_white;
            result += format_inline(heading_text);
            result += ansi::reset;
        }
        return result;
    }

    // ---- Blockquote ----
    if (!trimmed.empty() && trimmed[0] == '>') {
        size_t content_start = 1;
        if (content_start < trimmed.size() && trimmed[content_start] == ' ')
            content_start++;

        std::string result;
        result += ansi::dim;
        result += ansi::fg_magenta;
        result += ansi::pipe_block;
        result += " ";
        result += format_inline(trimmed.substr(content_start));
        result += ansi::reset;
        return result;
    }

    // ---- Unordered list ----
    if ((trimmed.size() >= 2 && (trimmed.substr(0,2) == "- " ||
                                  trimmed.substr(0,2) == "* " ||
                                  trimmed.substr(0,2) == "+ ")) ||
        (trimmed.size() >= 3 && (trimmed.substr(0,3) == "  - " ||
                                  trimmed.substr(0,3) == "  * " ||
                                  trimmed.substr(0,3) == "  + "))) {
        size_t indent = 0;
        while (indent < trimmed.size() && trimmed[indent] == ' ') indent++;
        std::string bullet_str = indent > 0 ? std::string("  ") + ansi::bullet
                                            : std::string(ansi::bullet);

        size_t content_start = indent + 2;
        std::string result;
        result += ansi::fg_cyan;
        result += std::string(indent, ' ');
        result += bullet_str;
        result += " ";
        result += ansi::reset;
        result += format_inline(trimmed.substr(content_start));
        return result;
    }

    // ---- Ordered list (1. 2. etc.) ----
    {
        size_t pos = 0;
        while (pos < trimmed.size() && trimmed[pos] == ' ') pos++;
        size_t num_end = pos;
        while (num_end < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[num_end])))
            num_end++;
        if (num_end > pos && num_end < trimmed.size() && trimmed[num_end] == '.' &&
            (num_end + 1 < trimmed.size() && trimmed[num_end + 1] == ' ')) {
            std::string result;
            result += ansi::fg_cyan;
            result += std::string(pos, ' ');
            result += trimmed.substr(pos, num_end - pos + 1);
            result += " ";
            result += ansi::reset;
            result += format_inline(trimmed.substr(num_end + 2));
            return result;
        }
    }

    // ---- Horizontal rule ----
    if (trimmed == "---" || trimmed == "***" || trimmed == "___") {
        return std::string(ansi::dim) + make_hr(10) + ansi::reset;
    }

    // ---- Plain text with inline formatting ----
    return format_inline(trimmed);
}

// ---- StreamingRenderer ----

// Full rendering: buffers partial lines, renders complete lines with markdown.
// Used by display_full_response (non-streaming).
std::string StreamingRenderer::feed(std::string_view chunk) {
    std::string output;
    for (char c : chunk) {
        if (c == '\n') {
            output += render_line(buffer_, state_);
            output += '\n';
            buffer_.clear();
        } else {
            buffer_ += c;
        }
    }
    return output;
}

// Raw streaming: characters output immediately for real-time token-by-token feel.
// Block-level state (code blocks) is tracked via render_line() but its
// formatted output is discarded — only raw characters are emitted.
std::string StreamingRenderer::feedRaw(std::string_view chunk) {
    std::string output;
    std::string line_buf;
    for (char c : chunk) {
        output += c;  // emit immediately for real-time feel
        if (c == '\n') {
            // Track block-level state from the complete line (discard rendered output)
            render_line(line_buf, state_);
            line_buf.clear();
        } else {
            line_buf += c;
        }
    }
    return output;
}

std::string StreamingRenderer::flush() {
    std::string output;
    // Flush any remaining buffered line (from feed())
    if (!buffer_.empty()) {
        output += render_line(buffer_, state_);
        output += '\n';
        buffer_.clear();
    }
    // Close any open code block
    if (state_.in_code_block) {
        state_.in_code_block = false;
        output += ansi::dim;
        output += std::string(ansi::dash) + std::string(ansi::dash) +
                  std::string(ansi::dash) + " end code " +
                  std::string(ansi::dash) + std::string(ansi::dash) +
                  std::string(ansi::dash);
        output += ansi::reset;
        output += '\n';
    }
    return output;
}

} // namespace markdown
