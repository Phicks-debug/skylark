# tiny-habibi

A fast, agentic CLI for running Gemma models locally via [LiteRT-LM](https://github.com/google-ai-edge/LiteRT-LM), with Metal GPU acceleration on macOS and Linux.

**Command:** `bb`

## Features

- **Streaming chat** with token-by-token output and **Markdown rendering** in terminal
- **Voice input** — record audio and send to the model
- **Image & video input** — attach images/video frames to messages
- **Web search** — Tavily-powered search tool (always enabled, set `TAVILY_API_KEY`)
- **Bash tool** — execute shell commands with human-in-the-loop permission control
- **Dynamic date/time** — system prompt includes current date/time for better context
- **Conversation persistence** — save/resume conversations with SQLite database
- **/permissions command** — toggle between Ask (confirm before bash) and Bypass modes

## Tools

Tools are **always enabled** — no `--search` flag needed.

### web_search
Search the internet for up-to-date information using Tavily. Set your API key:
```bash
export TAVILY_API_KEY=your_api_key_here
bb
```

### bash
Execute shell commands locally. Two permission modes:
- **Ask** (default): Prompts for confirmation before executing
- **Bypass**: Executes without confirmation

Toggle modes with `/permissions` command in the chat.

## Installation

One-line install (no build required — downloads a pre-built binary):

```bash
curl -fsSL https://raw.githubusercontent.com/Phicks-debug/tiny-habibi/main/install.sh | bash
```

Or with a custom repository:

```bash
bash install.sh --repo YOUR_USER/YOUR_REPO
```

The binary installs to `~/.local/bin/bb`. Add it to your PATH:

```bash
export PATH=\"$HOME/.local/bin:$PATH\"
```

### Prerequisites for the binary

- **Python 3.9+** with `litert-lm` installed:

  ```bash
  pip install litert-lm
  ```

- **TAVILY_API_KEY** environment variable for web search (optional):

  ```bash
  export TAVILY_API_KEY=your_api_key
  ```

> **Note:** `portaudio` / `libportaudio2` / `libcurl` are **only** needed when **building from source**. The pre-built binary links everything statically or bundles it.

## Quick Start

```bash
# Just run — auto-downloads Gemma 4 E2B model on first use
bb

# Use a specific model
bb --model ~/.cache/huggingface/hub/models--litert-community--gemma-4-E2B-it-litert-lm

# Inline 1-shot prompt — single query and exit
bb "What is 2+2?"
bb "Search for latest AI news"
bb "What is the capital of France?"

# Download a different model from HuggingFace
bb --model google/gemma-3-4b-it --download

# Voice input mode
bb --voice

# Debug mode (shows model info, system prompt, thinking channels)
bb --debug

# Attach an image
bb --image photo.jpg

# Set Tavily API key for web search
export TAVILY_API_KEY=your_key
bb
```

## Commands

| Command | Description |
|---|---|
| `/exit`, `/quit` | Exit the chat |
| `/help` | Show available commands |
| `/clear` | Clear conversation and start fresh |
| `/model` | Show current model and backend |
| `/resume` | List and resume previous conversations |
| `/delete` | Delete saved conversations |
| `/permissions` | Toggle bash permission mode (Ask/Bypass) |

### Conversation Management

- **/resume** — Shows list of saved conversations with timestamps. Select one to continue chatting with full context.
- **/delete** — Shows list with current conversation highlighted. Delete old conversations to save space.
- Conversations are automatically saved to `~/.cache/tiny-habibi/conversations.db`

## Platform Support

| Platform | Architecture | Pre-built Binary | Build from Source |
| --- | --- | --- | --- |
| **macOS** | Apple Silicon (arm64) | ✅ `tiny-habibi-darwin-arm64.tar.gz` | ✅ |
| **macOS** | Intel (x86_64) | ❌ (build from source) | ✅ |
| **Linux** | x86_64 | ✅ `tiny-habibi-linux-x64.tar.gz` | ✅ |
| **Linux** | aarch64 (ARM) | ❌ (build from source) | ✅ |
| **Windows** | x86_64 | ❌ (build from source) | Planned |

Pre-built binaries are available on the [Releases](https://github.com/Phicks-debug/tiny-habibi/releases) page.

## Building from Source

```bash
# 1. Install litert-lm (use the Python where you want it installed)
pip install litert-lm

# 2. Install system dependencies (build-time only)

# macOS:
brew install curl portaudio cmake

# Linux:
sudo apt install libcurl4-openssl-dev portaudio19-dev pkg-config cmake

# 3. Build — uses the Python that has litert-lm installed
LITERT_LM_DIR=$(python3 -c \"import litert_lm, os; print(os.path.dirname(litert_lm.__file__))\") \\
  cmake -B build -DCMAKE_BUILD_TYPE=Release -DLITERT_LM_DIR=\"$LITERT_LM_DIR\"
cmake --build build --parallel $(sysctl -n hw.logicalcpu 2>/dev/null || nproc)

# 4. Run
./build/bb --help
```

> **Troubleshooting:** If `python3` can't import `litert_lm`, try the specific Python version where it's installed (e.g., `python3.11`, `python3.12`). You can also pass the litert_lm path directly: `cmake -B build -DLITERT_LM_DIR=/path/to/litert_lm`. CMake will auto-detect litert_lm via `pip show litert-lm` as a fallback.

## Options

| Option | Description |
|---|---|
| `--model PATH` | Model path or HuggingFace repo ID (default: `litert-community/gemma-4-E2B-it-litert-lm`) |
| `bb "prompt"` | Inline 1-shot prompt — single query and exit (no interactive chat) |
| `--backend BACKEND` | Hardware backend: `cpu` or `gpu` (default: `gpu`) |
| `--voice, -v` | Enable voice input mode |
| `--image PATH` | Attach image to first message |
| `--video PATH` | Attach video (first frame) to first message |
| `--debug` | Print debug info (model, system prompt, etc.) |
| `--no-stream` | Disable streaming output |
| `--no-thinking` | Disable model thinking/reasoning mode |
| `--download` | Download model from HuggingFace and exit |
| `--system-prompt PATH` | Path to custom system prompt file |
| `--max-tokens N` | Maximum output tokens (default: 4096) |
| `--top-p P` | Top-P sampling (default: 0.95) |
| `--temperature T` | Sampling temperature (default: 1.0) |

> **Note:** Web search and bash tools are **always enabled**. Set `TAVILY_API_KEY` environment variable for web search to work.

## License

MIT