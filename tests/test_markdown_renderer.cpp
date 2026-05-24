// Standalone test for markdown renderer — no model needed
#include "markdown_renderer.hpp"
#include <iostream>
#include <sstream>
#include <vector>

// Helper: render a full multi-line markdown string and print with ANSI codes
static void test_markdown(const std::string& label, const std::string& input) {
    std::cout << "\n\033[1;36m=== " << label << " ===\033[0m\n";
    std::cout << "\033[2mInput:\033[0m\n" << input << "\n";
    std::cout << "\033[2mOutput:\033[0m\n";

    markdown::StreamingRenderer sr;
    std::string output = sr.feed(input);
    output += sr.flush();
    std::cout << output << std::flush;
    std::cout << "\n\033[2m--- end ---\033[0m\n";
}

int main() {
    std::cout << "\033[1;33m╔══════════════════════════════════════╗\033[0m\n";
    std::cout << "\033[1;33m║   Markdown Renderer Test Suite       ║\033[0m\n";
    std::cout << "\033[1;33m╚══════════════════════════════════════╝\033[0m\n";

    // 1. Headings
    test_markdown("H1 Heading", "# Hello World\n");
    test_markdown("H2 Heading", "## Section Two\n");
    test_markdown("H3 Heading", "### Subsection\n");

    // 2. Bold + Italic
    test_markdown("Bold text", "This is **bold** and this is __also bold__.\n");
    test_markdown("Italic text", "This is *italic* and normal.\n");
    test_markdown("Bold+Italic combo", "Some **bold** text with *italic* mixed in.\n");

    // 3. Inline code
    test_markdown("Inline code", "Use the `bb --help` command to see options.\n");

    // 4. Strikethrough
    test_markdown("Strikethrough", "This is ~~no longer relevant~~ updated info.\n");

    // 5. Links
    test_markdown("Links", "See the [LiteRT-LM docs](https://github.com/google-ai-edge/LiteRT-LM) for more.\n");

    // 6. Unordered lists
    test_markdown("Unordered list", "- First item\n- Second item\n- Third item\n");

    // 7. Ordered lists
    test_markdown("Ordered list", "1. Step one\n2. Step two\n3. Step three\n");

    // 8. Blockquotes
    test_markdown("Blockquote", "> This is a quoted block\n> It spans multiple lines\n");

    // 9. Code blocks
    test_markdown("Code block", "Here is some code:\n```python\ndef hello():\n    print(\"Hi\")\n```\nAfter the code block.\n");

    // 10. Horizontal rule
    test_markdown("Horizontal rule", "Above the rule\n---\nBelow the rule\n");

    // 11. Mixed content (simulating LLM output)
    test_markdown("Mixed LLM output",
        "# Analysis Results\n\n"
        "Here is a **summary** of the findings:\n\n"
        "- *Point one*: The `litert-lm` library supports **Gemma 4** models.\n"
        "- *Point two*: GPU acceleration works on macOS with Metal.\n\n"
        "> **Note:** Make sure to install `pip install litert-lm` first.\n\n"
        "For more details, see the [official docs](https://example.com).\n\n"
        "```bash\n"
        "bb --model /path/to/model --debug\n"
        "```\n\n"
        "That's all ~~folks~~ for now!\n");

    std::cout << "\n\033[1;32m✓ All markdown tests complete!\033[0m\n";
    return 0;
}
