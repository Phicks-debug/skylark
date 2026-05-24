# Tiny-Habibi

A fast, feature-rich CLI for running Gemma language models locally via [LiteRT-LM](https://github.com/google-ai-edge/LiteRT-LM), with Metal GPU acceleration on macOS.

## Features

- **Streaming chat** with token-by-token output
- **Thinking/reasoning mode** — Gemma 4 E2B model reasoning displayed in yellow
- **Voice input** — record audio and send to the model (PortAudio)
- **Image & video input** — attach images/video frames to messages
- **Web search** — Tavily-powered search tool integration
- **Model download** — pull models directly from HuggingFace
- **GPU backend** — Metal on macOS, OpenCL/Vulkan on Linux

## Installation

One-line install:
```bash
curl -fsSL https://raw.githubusercontent.com/Phicks-debug/tiny-habibi/main/install.sh | bash
```

Or with a custom repository:
```bash
bash install.sh --repo YOUR_USER/YOUR_REPO
```

The binary installs to `~/.local/bin/bb`. Add it to your PATH:
```bash
export PATH="$HOME/.local/bin:$PATH"
```

### Prerequisites

- **Python 3.11+** with `litert-lm` installed:
  ```bash
  pip install litert-lm
  ```
- **macOS:** Xcode Command Line Tools (`xcode-select --install`)
- **Linux:** `sudo apt install libcurl4-openssl-dev portaudio19-dev libportaudio2`

## Quick Start

```bash
# Basic chat with a model
bb --model ~/.cache/huggingface/hub/models--litert-community--gemma-4-E2B-it-litert-lm

# Download a model from HuggingFace
bb --model google/gemma-3-4b-it --download

# Chat with web search enabled
bb --model /path/to/model.litertlm --search --tavily-key YOUR_KEY

# Voice input mode
bb --model /path/to/model --voice

# Debug mode (shows model info, system prompt, thinking channels)
bb --model /path/to/model --debug

# Attach an image
bb --model /path/to/model --image photo.jpg
```

## Building from Source

```bash
# Install deps
pip install litert-lm

# macOS
brew install curl portaudio

# Linux
sudo apt install libcurl4-openssl-dev portaudio19-dev pkg-config

# Build
LITERT_LM_DIR=$(python3 -c "import litert_lm, os; print(os.path.dirname(litert_lm.__file__))") \
  cmake -B build -DCMAKE_BUILD_TYPE=Release -DLITERT_LM_DIR="$LITERT_LM_DIR"
cmake --build build --parallel $(sysctl -n hw.logicalcpu 2>/dev/null || nproc)

# Run
./build/bb --help
```

## Options

| Option | Description |
|---|---|
| `--model PATH` | Model path or HuggingFace repo ID |
| `--backend BACKEND` | Hardware backend: `cpu` or `gpu` (default: `gpu`) |
| `--voice, -v` | Enable voice input mode |
| `--image PATH` | Attach image to first message |
| `--video PATH` | Attach video (first frame) to first message |
| `--search` | Enable Tavily web search tool |
| `--tavily-key KEY` | Tavily API key (or set `TAVILY_API_KEY` env var) |
| `--debug` | Print debug info (model, system prompt, etc.) |
| `--no-stream` | Disable streaming output |
| `--no-thinking` | Disable model thinking/reasoning mode |
| `--download` | Download model from HuggingFace and exit |
| `--system-prompt PATH` | Path to custom system prompt file |
| `--max-tokens N` | Maximum output tokens (default: 4096) |
| `--top-p P` | Top-P sampling (default: 0.95) |
| `--temperature T` | Sampling temperature (default: 1.0) |

## Download

Pre-built binaries are available on the [Releases](https://github.com/Phicks-debug/tiny-habibi/releases) page:

- `tiny-habibi-darwin-arm64.tar.gz` — macOS Apple Silicon
- `tiny-habibi-linux-x64.tar.gz` — Linux x86_64

## License

MIT
