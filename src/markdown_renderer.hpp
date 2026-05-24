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

// Render a chunk of streaming text. Buffers partial lines and
// renders complete lines. Returns formatted output ready to print.
// Call flush() at the end to get any remaining buffered text.
class StreamingRenderer {
public:
    // Feed a chunk; returns formatted text (may be empty if line incomplete)
    std::string feed(std::string_view chunk);

    // Get any remaining buffered text after all chunks are done
    std::string flush();

private:
    RenderState state_;
    std::string buffer_;
};

} // namespace markdown

#endif // MARKDOWN_RENDERER_HPP
