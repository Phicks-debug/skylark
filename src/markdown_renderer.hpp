// Markdown-to-ANSI renderer for terminal output
// Handles: bold, italic, code, headings, code blocks, lists, blockquotes

#ifndef MARKDOWN_RENDERER_HPP
#define MARKDOWN_RENDERER_HPP

#include <string>
#include <string_view>

namespace markdown {

// State machine for block-level markdown (code blocks)
struct RenderState {
    bool in_code_block = false;
    std::string code_block_lang;  // language hint from opening ```
};

// Render a single line of markdown text with ANSI formatting.
// Updates state for block-level constructs (code blocks).
// Returns the ANSI-formatted string (without trailing newline).
std::string render_line(std::string_view line, RenderState& state);

// Renders streaming text. Two modes:
// - feed(): renders complete lines with full markdown (for non-streaming / final display)
// - feedRaw(): outputs raw chars immediately + tracks block state (for streaming token-by-token)
// Call flush() at the end to close any open constructs.
class StreamingRenderer {
public:
    // Feed a chunk; buffers and renders complete lines with markdown formatting.
    std::string feed(std::string_view chunk);

    // Feed a chunk for real-time streaming: raw chars output immediately.
    // Block-level state (code blocks) is tracked internally.
    std::string feedRaw(std::string_view chunk);

    // Get any remaining buffered text and close open constructs
    std::string flush();

private:
    RenderState state_;
    std::string buffer_;  // used by feed() for line buffering
};

} // namespace markdown

#endif // MARKDOWN_RENDERER_HPP
