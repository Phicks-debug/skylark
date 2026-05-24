// tiny-habibi - C++ CLI for running Gemma models with LiteRT-LM
// Features: streaming chat, voice input, image/video input,
//            Tavily web search, model download, GPU backend,
//            SQLite conversation persistence with /resume

#include "litert_lm_c_api.h"
#include "terminal.hpp"
#include "model_downloader.hpp"
#include "audio_recorder.hpp"
#include "tavily_search.hpp"
#include "markdown_renderer.hpp"
#include "json_utils.hpp"
#include "conversation_db.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using namespace terminal;

// ---- Configuration ----
struct Config {
    std::string model_path;
    std::string backend = "gpu";       // cpu, gpu (Metal on macOS)
    std::string vision_backend;        // default: same as backend
    std::string audio_backend;         // default: same as backend
    std::string cache_dir;
    std::string system_prompt_path;    // path to AGENTS.md or custom prompt
    std::string tavily_api_key;
    std::string image_path;            // one-shot image input
    std::string video_path;            // one-shot video input (first frame)
    bool voice_mode = false;
    bool enable_search = false;
    bool no_stream = false;
    bool debug = false;
    bool no_thinking = false;
    bool speculative_decoding = false;
    bool download_only = false;
    int max_output_tokens = 4096;
    int max_num_tokens = 4096;
    int top_k = 64;
    float top_p = 0.95f;
    float temperature = 1.0f;
    int seed = 42;
};

// ---- Globals ----
static std::atomic<bool> g_cancellation_requested{false};

// ---- Signal handler for Ctrl+C (async-signal-safe: only sets atomic flag) ----
static void sigint_handler(int /*sig*/) {
    g_cancellation_requested.store(true, std::memory_order_release);
}

// ---- Help text ----
static void print_help(const char* prog) {
    std::cout << R"(tiny-habibi - Interactive CLI for Gemma models via LiteRT-LM

Usage: )" << prog << R"( [OPTIONS]

Model Options:
  --model PATH           Model path or HuggingFace repo ID (e.g., "google/gemma-3-4b-it")
  --backend BACKEND      Hardware backend: cpu or gpu (default: gpu)
  --cache-dir DIR        Cache directory for downloaded models
                         (default: ~/.cache/tiny-habibi/models)
  --download             Download model from HuggingFace and exit

Inference Options:
  --max-tokens N         Maximum output tokens (default: 4096)
  --top-k K              Top-K sampling parameter (default: 64)
  --top-p P              Top-P sampling parameter (default: 0.95)
  --temperature T        Temperature for sampling (default: 1.0)
  --seed S               Random seed (default: 42)
  --speculative          Enable speculative decoding (MTP)
  --no-stream            Disable streaming output

Input Options:
  --voice, -v            Enable voice input mode (press ENTER to stop recording)
  --image PATH           Attach an image to the first message
  --video PATH           Attach a video to the first message (uses first frame)
  --no-thinking          Disable model thinking/reasoning mode

Tool Options:
  --search               Enable Tavily web search tool
                         (requires TAVILY_API_KEY environment variable)

System Options:
  --system-prompt PATH   Path to system prompt file (default: AGENTS.md in CWD)
  --debug                Print debug info (model, system prompt, chat template, etc.)
  --help, -h             Show this help message

Examples:
  )" << prog << R"( --model /path/to/model.safetensors
  )" << prog << R"( --model google/gemma-3-4b-it --download
  )" << prog << R"( --model /path/to/model --voice --search
  )" << prog << R"( --model /path/to/model --image photo.jpg
)" << std::endl;
}

// ---- Parse CLI arguments ----
static Config parse_args(int argc, char* argv[]) {
    Config cfg;

    // Read TAVILY_API_KEY from environment as default (CLI flag overrides)
    const char* env_key = std::getenv("TAVILY_API_KEY");
    if (env_key && env_key[0] != '\0') {
        cfg.tavily_api_key = env_key;
    }

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        auto next = [&]() -> const char* {
            return (i + 1 < argc) ? argv[++i] : "";
        };

        if (arg == "--help" || arg == "-h") {
            print_help(argv[0]);
            std::exit(0);
        } else if (arg == "--model")              cfg.model_path = next();
        else if (arg == "--backend")              cfg.backend = next();
        else if (arg == "--cache-dir")            cfg.cache_dir = next();
        else if (arg == "--max-tokens") {
            try { cfg.max_output_tokens = std::stoi(next()); }
            catch (...) { std::cerr << "Invalid value for --max-tokens\n"; std::exit(1); }
        }
        else if (arg == "--top-k") {
            try { cfg.top_k = std::stoi(next()); }
            catch (...) { std::cerr << "Invalid value for --top-k\n"; std::exit(1); }
        }
        else if (arg == "--top-p") {
            try { cfg.top_p = std::stof(next()); }
            catch (...) { std::cerr << "Invalid value for --top-p\n"; std::exit(1); }
        }
        else if (arg == "--temperature") {
            try { cfg.temperature = std::stof(next()); }
            catch (...) { std::cerr << "Invalid value for --temperature\n"; std::exit(1); }
        }
        else if (arg == "--seed") {
            try { cfg.seed = std::stoi(next()); }
            catch (...) { std::cerr << "Invalid value for --seed\n"; std::exit(1); }
        }
        else if (arg == "--speculative")          cfg.speculative_decoding = true;
        else if (arg == "--no-stream")            cfg.no_stream = true;
        else if (arg == "--voice" || arg == "-v") cfg.voice_mode = true;
        else if (arg == "--image")                cfg.image_path = next();
        else if (arg == "--video")                cfg.video_path = next();
        else if (arg == "--search")               cfg.enable_search = true;
        else if (arg == "--download")             cfg.download_only = true;
        else if (arg == "--system-prompt")        cfg.system_prompt_path = next();
        else if (arg == "--no-thinking")          cfg.no_thinking = true;
        else if (arg == "--debug")                  cfg.debug = true;
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            std::cerr << "Try '" << argv[0] << " --help' for usage.\n";
            std::exit(1);
        }
    }

    return cfg;
}

// ---- Read AGENTS.md or system prompt file ----
static std::string read_system_prompt(const Config& cfg) {
    // Try cfg.system_prompt_path first
    if (!cfg.system_prompt_path.empty() && fs::exists(cfg.system_prompt_path)) {
        std::ifstream file(cfg.system_prompt_path);
        if (file.is_open()) {
            std::ostringstream ss;
            ss << file.rdbuf();
            std::string content = ss.str();
            // Trim whitespace
            auto start = content.find_first_not_of(" \t\n\r");
            auto end = content.find_last_not_of(" \t\n\r");
            if (start != std::string::npos) {
                return content.substr(start, end - start + 1);
            }
        }
    }

    // Try AGENTS.md in current directory
    if (fs::exists("AGENTS.md")) {
        std::ifstream file("AGENTS.md");
        if (file.is_open()) {
            std::ostringstream ss;
            ss << file.rdbuf();
            std::string content = ss.str();
            auto start = content.find_first_not_of(" \t\n\r");
            auto end = content.find_last_not_of(" \t\n\r");
            if (start != std::string::npos) {
                return content.substr(start, end - start + 1);
            }
        }
    }

    // Fallback: default system prompt with tool-use instructions
    return "You are a helpful assistant.\n\n"
           "You also have access to web search and web fetch tools. "
           "Always use these tools to research and get up-to-date information "
           "or when you are asked. Your knowledge is limited by the training "
           "data cutoff date so do not rely on it for up-to-date information "
           "or fact checks.\n\n"
           "Do not make assumptions based on your knowledge. Use the information "
           "from the tool results to answer. Do not add details or information "
           "that is not included in the tool results.\n\n"
           "When handling complex queries, break them down into structured plans "
           "before executing. First, identify what you need to know and in what "
           "order, determine which tool calls depend on others and which can run "
           "in parallel. For multi-hop reasoning, work step-by-step: answer each "
           "sub-question using tools, then use those answers to inform the next "
           "step. Do not try to answer everything in one go; instead, build up "
           "the answer through intermediate tool calls.\n\n"
           "If tools return an error or you don't receive or have the answer, "
           "answer you don't know. Do not answer or try to answer without the "
           "tool results.";
}

// ---- Resolve model path from a directory (HF cache or similar) ----
static std::string resolve_model_path(const std::string& path) {
    // If it's a regular file, use it directly
    if (fs::is_regular_file(path)) {
        return path;
    }

    // If it's not a directory, return empty
    if (!fs::is_directory(path)) {
        return "";
    }

    std::error_code ec;

    // First, look for *.litertlm files directly in the directory
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        if (ec) break;
        std::string name = entry.path().filename().string();
        if (entry.is_regular_file() || entry.is_symlink()) {
            // Must END with .litertlm (not .litertlm_<digits>.bin shards)
            if (name.size() >= 9 && name.compare(name.size() - 9, 9, ".litertlm") == 0) {
                return entry.path().string();
            }
        }
    }

    // HF cache structure: look in snapshots/<hash>/
    fs::path snapshots = fs::path(path) / "snapshots";
    if (fs::is_directory(snapshots, ec)) {
        for (const auto& snap_entry : fs::directory_iterator(snapshots, ec)) {
            if (ec) break;
            if (!snap_entry.is_directory()) continue;
            for (const auto& file_entry : fs::directory_iterator(snap_entry.path(), ec)) {
                if (ec) break;
                std::string name = file_entry.path().filename().string();
                if ((file_entry.is_regular_file() || file_entry.is_symlink()) &&
                    name.size() >= 9 && name.compare(name.size() - 9, 9, ".litertlm") == 0) {
                    return file_entry.path().string();
                }
            }
        }
    }

    // Also check for .safetensors file
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        if (ec) break;
        if (entry.is_regular_file() &&
            entry.path().extension() == ".safetensors") {
            return entry.path().string();
        }
    }

    return "";
}

// ---- Build a user message JSON for the LLM ----
// LiteRT-LM expects content as an array of typed parts:
// {"role":"user","content":[{"type":"text","text":"..."}]}
static std::string build_message_json(std::string_view text,
                                      std::string_view image_path,
                                      std::string_view audio_path) {
    std::ostringstream json;

    json << "{\"role\":\"user\",\"content\":[";

    bool first = true;

    // Text part
    if (!text.empty()) {
        json << "{\"type\":\"text\",\"text\":\""
             << json_utils::json_escape(text) << "\"}";
        first = false;
    }

    // Image part
    if (!image_path.empty()) {
        if (!first) json << ",";
        json << "{\"type\":\"image\",\"path\":\""
             << json_utils::json_escape(image_path) << "\"}";
        first = false;
    }

    // Audio part
    if (!audio_path.empty()) {
        if (!first) json << ",";
        json << "{\"type\":\"audio\",\"path\":\""
             << json_utils::json_escape(audio_path) << "\"}";
        first = false;
    }

    json << "]}";

    return json.str();
}

// ---- Extract tool call info from response JSON ----
struct ToolCall {
    std::string name;
    std::string arguments_json;  // raw JSON object string for arguments
};

static std::vector<ToolCall> extract_tool_calls(std::string_view json) {
    std::vector<ToolCall> calls;

    // Find "tool_calls" key
    auto tc_pos = json.find("\"tool_calls\"");
    if (tc_pos == std::string_view::npos) return calls;

    // Find the opening '[' after tool_calls
    auto arr_start = json.find('[', tc_pos);
    if (arr_start == std::string_view::npos) return calls;

    // Extract each object in the array
    size_t cur = arr_start + 1;
    while (cur < json.length()) {
        auto obj_start = json.find('{', cur);
        if (obj_start == std::string_view::npos) break;
        if (obj_start >= json.find(']', arr_start)) break;

        // Find matching '}'
        int depth = 0;
        size_t obj_end = obj_start;
        for (size_t i = obj_start; i < json.length(); i++) {
            if (json[i] == '{') depth++;
            else if (json[i] == '}') {
                depth--;
                if (depth == 0) { obj_end = i; break; }
            }
            if (json[i] == '"') {
                i++;
                while (i < json.length() && json[i] != '"') {
                    if (json[i] == '\\') i++;
                    i++;
                }
            }
        }
        if (depth != 0) break;

        std::string_view obj = json.substr(obj_start, obj_end - obj_start + 1);

        // Look for the "function" nested object
        auto func_pos = obj.find("\"function\"");
        if (func_pos != std::string_view::npos) {
            auto func_obj = obj.find('{', func_pos);
            if (func_obj != std::string_view::npos) {
                // Find matching close for function object
                int fd = 0;
                size_t func_end = func_obj;
                for (size_t i = func_obj; i < obj.length(); i++) {
                    if (obj[i] == '{') fd++;
                    else if (obj[i] == '}') { fd--; if (fd == 0) { func_end = i; break; } }
                    if (obj[i] == '"') { i++; while (i < obj.length() && obj[i] != '"') { if (obj[i] == '\\') i++; i++; } }
                }
                if (fd == 0) {
                    std::string_view func = obj.substr(func_obj, func_end - func_obj + 1);
                    ToolCall tc;
                    tc.name = json_utils::json_get_str(func, "name");
                    tc.arguments_json = json_utils::json_get_object(func, "arguments");
                    if (!tc.name.empty()) calls.push_back(std::move(tc));
                }
            }
        }

        cur = obj_end + 1;
    }

    return calls;
}

// ---- Execute tool calls and format response JSON ----
static std::string execute_tool_calls(const std::vector<ToolCall>& calls,
                                       std::string_view api_key) {
    std::ostringstream responses;
    responses << "[";

    for (size_t i = 0; i < calls.size(); i++) {
        if (i > 0) responses << ",";

        if (calls[i].name == "web_search") {
            // Parse query from arguments JSON
            std::string query = json_utils::json_get_str(calls[i].arguments_json, "query");
            if (query.empty()) {
                query = json_utils::json_get_str(calls[i].arguments_json, "q");
            }

            if (!query.empty() && !api_key.empty()) {
                terminal::cprintln("🔍 Searching: " + query, terminal::Color::Cyan);
                auto results = tavily_search::search(query, api_key, 5, "basic");
                std::string result_json = tavily_search::format_results_json(query, results);

                responses << "{\"role\":\"tool\",\"content\":["
                         << "{\"type\":\"tool_response\","
                         << "\"name\":\"web_search\","
                         << "\"response\":" << result_json
                         << "}]}";
            } else {
                responses << "{\"role\":\"tool\",\"content\":["
                         << "{\"type\":\"tool_response\","
                         << "\"name\":\"web_search\","
                         << "\"response\":\"Error: No query provided or API key missing.\""
                         << "}]}";
            }
        } else {
            // Unknown tool — send error
            responses << "{\"role\":\"tool\",\"content\":["
                     << "{\"type\":\"tool_response\","
                     << "\"name\":\"" << json_utils::json_escape(calls[i].name) << "\","
                     << "\"response\":\"Error: Unknown tool \\\"" << json_utils::json_escape(calls[i].name) << "\\\"\""
                     << "}]}";
        }
    }

    responses << "]";
    return responses.str();
}

// ---- Stream callback helpers ----
// send_message_stream is non-blocking: it returns immediately and invokes the
// callback from a background thread. StreamState uses a condition variable so
// the main thread waits until the callback signals completion (is_final or error).
struct StreamState {
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    bool printed_any = false;
    bool channel_active = false;   // true if currently streaming channel (thinking) content
    std::string accumulated;       // accumulate full JSON for tool detection
    markdown::StreamingRenderer md_renderer;       // markdown renderer for text output
};

// ---- Shared: parse a JSON array (already extracted) for text content ----
// Calls `on_text(str)` for each text segment found.
template <typename F>
static void parse_content_array(std::string_view content_arr, F&& on_text) {
    size_t search = 1;  // skip opening [
    while (search < content_arr.length()) {
        auto obj_start = content_arr.find('{', search);
        if (obj_start == std::string_view::npos) break;

        // Find matching close brace for this object
        int depth = 0;
        size_t obj_end = obj_start;
        for (size_t i = obj_start; i < content_arr.length(); i++) {
            if (content_arr[i] == '{') depth++;
            else if (content_arr[i] == '}') {
                depth--;
                if (depth == 0) { obj_end = i; break; }
            }
            if (content_arr[i] == '"') {
                i++;
                while (i < content_arr.length() && content_arr[i] != '"') {
                    if (content_arr[i] == '\\') i++;
                    i++;
                }
            }
        }
        if (depth != 0) break;

        std::string obj(content_arr.substr(obj_start, obj_end - obj_start + 1));

        // Check if this object has "type":"text"
        auto type_pos = obj.find("\"type\":\"text\"");
        if (type_pos != std::string::npos) {
            auto text_key = obj.find("\"text\":\"");
            if (text_key != std::string::npos) {
                auto text_start = text_key + 8;
                auto text_end = obj.find('"', text_start);
                if (text_end != std::string::npos) {
                    std::string raw(obj.substr(text_start, text_end - text_start));
                    if (!raw.empty()) {
                        // Unescape JSON string escapes (\n, \", \\, etc.)
                        on_text(json_utils::json_unescape(raw));
                    }
                }
            }
        }

        search = obj_end + 1;
    }
}

// ---- Extract all text from a JSON response (for saving to DB) ----
static std::string extract_response_text(std::string_view json) {
    std::ostringstream out;
    std::string content_arr = json_utils::json_get_array(json, "content");
    if (!content_arr.empty()) {
        bool first = true;
        parse_content_array(content_arr, [&](const std::string& text) {
            if (!first) out << ' ';
            out << text;
            first = false;
        });
    }
    return out.str();
}

// Channel content (e.g., thinking) streams raw characters immediately in gray.
// Text content streams raw characters immediately for token-by-token feel (like Ollama).
static void display_chunk(std::string_view chunk, StreamState& state) {
    if (chunk.empty()) return;

    // Non-JSON chunks: stream raw characters immediately
    if (chunk[0] != '{' && chunk[0] != '[') {
        if (state.channel_active) {
            std::cout << ansi(Color::Reset) << '\n';
            state.channel_active = false;
        }
        std::string formatted = state.md_renderer.feedRaw(chunk);
        if (!formatted.empty()) {
            std::cout << formatted;
            std::cout.flush();
            state.printed_any = true;
        }
        return;
    }

    // Parse channels: {"channels":{"channel_name":"content",...}}
    // Uses proper JSON string extraction that handles \" escapes.
    std::string channels_obj = json_utils::json_get_object(chunk, "channels");
    if (!channels_obj.empty()) {
        size_t pos = 1;  // skip opening {
        while (pos < channels_obj.length()) {
            // Extract channel name (quoted string) — needed to advance past it
            size_t kq = channels_obj.find('"', pos);
            if (kq == std::string::npos) break;
            size_t kqe;
            json_utils::json_extract_raw_string(channels_obj, kq, kqe);  // skip key

            auto colon = channels_obj.find(':', kqe);
            if (colon == std::string::npos) break;

            // Extract channel value (quoted string) with proper escape handling
            size_t vq = channels_obj.find('"', colon + 1);
            if (vq == std::string::npos) break;
            size_t vqe;
            std::string raw_content = json_utils::json_extract_raw_string(channels_obj, vq, vqe);

            if (!raw_content.empty()) {
                std::string unescaped = json_utils::json_unescape(raw_content);
                // Stream channel content immediately in gray, no markdown/line-buffering
                if (!state.channel_active) {
                    if (state.printed_any) {
                        std::string leftover = state.md_renderer.flush();
                        if (!leftover.empty()) std::cout << leftover;
                        std::cout << '\n';
                    }
                    std::cout << ansi(Color::BrightBlack);
                    state.channel_active = true;
                }
                std::cout << unescaped;
                std::cout.flush();
                state.printed_any = true;
            }

            pos = vqe;
        }
    }

    // Show tool call indicator if chunk contains tool_calls
    if (chunk.find("\"tool_calls\"") != std::string_view::npos) {
        if (state.channel_active) {
            std::cout << ansi(Color::Reset) << '\n';
            state.channel_active = false;
        }
        std::string leftover = state.md_renderer.flush();
        if (!leftover.empty()) std::cout << leftover;
        if (state.printed_any) std::cout << '\n';
        cprint("🔧 Tool call requested...", Color::Cyan);
        state.printed_any = true;
    }

    // Parse text from content array: {"content":[{"type":"text","text":"..."}]}
    std::string content_arr = json_utils::json_get_array(chunk, "content");
    if (!content_arr.empty()) {
        parse_content_array(content_arr, [&](const std::string& text) {
            if (state.channel_active) {
                std::cout << ansi(Color::Reset) << '\n';
                state.channel_active = false;
            }
            std::string formatted = state.md_renderer.feedRaw(text);
            if (!formatted.empty()) {
                std::cout << formatted;
                std::cout.flush();
                state.printed_any = true;
            }
        });
    }
}

// ---- Stream callback for conversation_send_message_stream ----
// Called from a background thread. Signals completion via StreamState::cv.
static void stream_callback(void* data,
                            const char* chunk,
                            bool is_final,
                            const char* error_msg) {
    auto* state = static_cast<StreamState*>(data);

    if (error_msg && strlen(error_msg) > 0) {
        cprint(std::string("[Error: ") + error_msg + "]\n", Color::Red);
        // Signal completion on error
        {
            std::lock_guard<std::mutex> lock(state->mtx);
            state->done = true;
        }
        state->cv.notify_one();
        return;
    }

    if (chunk) {
        std::string chunk_str(chunk);
        // Display with channel/text parsing
        display_chunk(chunk_str, *state);
        // Accumulate for tool call detection
        state->accumulated += chunk_str;
    }

    if (is_final) {
        if (state->printed_any) {
            // Close channel color if still active
            if (state->channel_active) {
                std::cout << ansi(Color::Reset) << '\n';
                state->channel_active = false;
            }
            // Flush any remaining markdown buffer (closes code blocks, etc.)
            std::string leftover = state->md_renderer.flush();
            if (!leftover.empty()) std::cout << leftover;
            std::cout << '\n';
            std::cout.flush();
        }
        // Signal completion
        {
            std::lock_guard<std::mutex> lock(state->mtx);
            state->done = true;
        }
        state->cv.notify_one();
    }
}

// ---- Download model from HuggingFace ----
static bool download_model(const Config& cfg) {
    if (model_downloader::is_local_path(cfg.model_path)) {
        cprintln("Model path is already a local file: " + cfg.model_path, Color::Green);
        return true;
    }

    cprintln("📥 Downloading model from HuggingFace: " + cfg.model_path, Color::Cyan);

    // Construct download URL
    std::string base_url = "https://huggingface.co/" + cfg.model_path + "/resolve/main/";

    // List of common model file names to try
    std::vector<std::string> candidates = {
        "model.safetensors",
        "pytorch_model.bin",
        "model.gguf",
    };

    std::string cache = cfg.cache_dir.empty()
        ? model_downloader::default_cache_dir()
        : cfg.cache_dir;

    // Create model-specific cache dir
    std::string model_dir = cache + "/" + cfg.model_path;
    std::replace(model_dir.begin(), model_dir.end(), '/', '_');
    fs::create_directories(model_dir);

    for (const auto& file : candidates) {
        std::string url = base_url + file;
        std::string output = model_dir + "/" + file;

        cprintln("Trying: " + url, Color::Dim);

        auto progress_fn = [](size_t downloaded, size_t total) {
            if (total > 0) {
                int pct = static_cast<int>(100.0 * downloaded / total);
                if (pct % 10 == 0 || pct == 100) {
                    std::cout << "\r  Downloading... " << pct << "%" << std::flush;
                }
            }
        };

        if (model_downloader::download_file(url, output, progress_fn)) {
            std::cout << '\n';
            cprintln("✅ Downloaded: " + output, Color::Green);
            return true;
        }
    }

    cprintln("❌ Failed to download model. Check the model ID and your internet connection.",
            Color::Red);
    return false;
}

// Display a complete (non-streaming) JSON response with channel/text parsing
// Text content goes through markdown renderer for rich terminal output.
static void display_full_response(std::string_view json) {
    if (json.empty()) return;

    // Parse channels: {"channels":{"channel_name":"content",...}}
    std::string channels_obj = json_utils::json_get_object(json, "channels");
    if (!channels_obj.empty()) {
        size_t pos = 1;  // skip opening {
        while (pos < channels_obj.length()) {
            // Extract channel name (quoted string) — needed to advance past it
            size_t kq = channels_obj.find('"', pos);
            if (kq == std::string::npos) break;
            size_t kqe;
            json_utils::json_extract_raw_string(channels_obj, kq, kqe);  // skip key

            auto colon = channels_obj.find(':', kqe);
            if (colon == std::string::npos) break;

            // Extract channel value (quoted string) with proper escape handling
            size_t vq = channels_obj.find('"', colon + 1);
            if (vq == std::string::npos) break;
            size_t vqe;
            std::string ch_content = json_utils::json_extract_raw_string(channels_obj, vq, vqe);

            if (!ch_content.empty()) {
                std::string unescaped = json_utils::json_unescape(ch_content);
                // Output channel content as raw gray text, no markdown rendering
                std::cout << ansi(Color::BrightBlack)
                          << unescaped
                          << ansi(Color::Reset) << '\n';
            }

            pos = vqe;
        }
    }

    // Show tool call indicator if response contains tool_calls
    if (json.find("\"tool_calls\"") != std::string_view::npos) {
        cprint("🔧 Tool call requested...", Color::Cyan);
        std::cout << '\n';
    }

    // Parse text from content array: {"content":[{"type":"text","text":"..."}]}
    std::string content_arr = json_utils::json_get_array(json, "content");
    if (!content_arr.empty()) {
        parse_content_array(content_arr, [&](const std::string& text) {
            // text is already unescaped by parse_content_array
            markdown::StreamingRenderer sr;
            std::cout << sr.feed(text);
            std::cout << sr.flush();
        });
        std::cout << '\n';
    }
}

// ---- Print debug info (model, system prompt, chat template, etc.) ----
static void print_debug_info(const Config& cfg,
                              const std::string& resolved_model_path,
                              const std::string& system_prompt) {
    std::cout << "\n";
    print_separator(Color::Cyan);
    cprintln("  🔍 DEBUG INFO", Color::BoldCyan);
    print_separator(Color::Cyan);

    // Model
    cprintln("Model:", Color::BoldWhite);
    std::cout << "  path:        " << resolved_model_path << "\n";
    std::cout << "  backend:     " << cfg.backend << "\n";
    std::cout << "  vision_be:   " << (cfg.vision_backend.empty() ? "cpu (default)" : cfg.vision_backend) << "\n";
    std::cout << "  audio_be:    " << (cfg.audio_backend.empty() ? "cpu (default)" : cfg.audio_backend) << "\n";

    // Inference params
    cprintln("\nInference parameters:", Color::BoldWhite);
    std::cout << "  max_output_tokens: " << cfg.max_output_tokens << "\n";
    std::cout << "  max_num_tokens:    " << cfg.max_num_tokens << "\n";
    std::cout << "  top_k:             " << cfg.top_k << "\n";
    std::cout << "  top_p:             " << cfg.top_p << "\n";
    std::cout << "  temperature:       " << cfg.temperature << "\n";
    std::cout << "  seed:              " << cfg.seed << "\n";
    std::cout << "  speculative:       " << (cfg.speculative_decoding ? "yes" : "no") << "\n";

    // System prompt
    cprintln("\nSystem prompt:", Color::BoldWhite);
    std::cout << "  source:     ";
    if (!cfg.system_prompt_path.empty()) {
        std::cout << cfg.system_prompt_path << "\n";
    } else if (fs::exists("AGENTS.md")) {
        std::cout << "AGENTS.md (cwd)\n";
    } else {
        std::cout << "(default inline)\n";
    }
    std::cout << "  length:     " << system_prompt.length() << " chars\n";
    std::cout << "  content:\n";
    // Print system prompt with a prefix on each line
    std::string_view sv(system_prompt);
    size_t max_preview = 1500;
    if (sv.length() > max_preview) {
        std::cout << "  [first " << max_preview << " chars]\n";
        std::string preview(sv.substr(0, max_preview));
        for (char c : preview) {
            std::cout << c;
            if (c == '\n') std::cout << "  ";
        }
        std::cout << "\n  [... truncated, " << sv.length() - max_preview << " more chars]";
    } else {
        std::cout << "  ";
        for (char c : sv) {
            std::cout << c;
            if (c == '\n') std::cout << "  ";
        }
    }
    std::cout << "\n";

    // Chat template
    cprintln("\nChat template:", Color::BoldWhite);
    std::cout << "  apply_prompt_template: yes (Jinja from model metadata)\n";
    std::cout << "  note:                 Gemma 4 uses Jinja chat template from model config\n";

    // Thinking mode
    cprintln("\nThinking mode:", Color::BoldWhite);
    std::cout << "  enabled:      " << (!cfg.no_thinking ? "yes (reasoning channels on)" : "no (disabled via --no-thinking)") << "\n";
    std::cout << "  extra_context: {\"enable_thinking\": " << (!cfg.no_thinking ? "true" : "false") << "}\n";

    // Tools
    cprintln("\nTools:", Color::BoldWhite);
    if (cfg.enable_search) {
        std::cout << "  web_search:   enabled\n";
        std::cout << "  tavily_key:   " << (cfg.tavily_api_key.empty() ? "(not set)" : "[set]") << "\n";
        std::string tools_def = tavily_search::get_tool_definition();
        std::cout << "  tools_def:    " << tools_def.length() << " chars JSON\n";
    } else {
        std::cout << "  web_search:   disabled\n";
    }

    // Input modes
    cprintln("\nInput modes:", Color::BoldWhite);
    std::cout << "  voice:        " << (cfg.voice_mode ? "enabled" : "disabled") << "\n";
    std::cout << "  image:        " << (cfg.image_path.empty() ? "(none)" : cfg.image_path) << "\n";
    std::cout << "  video:        " << (cfg.video_path.empty() ? "(none)" : cfg.video_path) << "\n";

    // Streaming
    cprintln("\nStreaming:", Color::BoldWhite);
    std::cout << "  mode:         " << (cfg.no_stream ? "disabled (full responses only)" : "enabled (streaming token output)") << "\n";

    print_separator(Color::Cyan);
    std::cout << "\n";
}

// ---- Helper: create a new conversation with DB history loaded ----
static LiteRtLmConversation* create_conversation_with_history(
    LiteRtLmEngine* engine,
    const Config& cfg,
    const std::string& system_prompt,
    const std::vector<conversation_db::MessageInfo>& history) {

    LiteRtLmSessionConfig* session_cfg = litert_lm_session_config_create();
    litert_lm_session_config_set_max_output_tokens(session_cfg, cfg.max_output_tokens);
    litert_lm_session_config_set_apply_prompt_template(session_cfg, true);

    LiteRtLmSamplerParams sampler = {
        kLiteRtLmSamplerTypeTopP,
        cfg.top_k,
        cfg.top_p,
        cfg.temperature,
        cfg.seed,
    };
    litert_lm_session_config_set_sampler_params(session_cfg, &sampler);

    LiteRtLmConversationConfig* conv_cfg = litert_lm_conversation_config_create();
    litert_lm_conversation_config_set_session_config(conv_cfg, session_cfg);
    litert_lm_session_config_delete(session_cfg);

    // Set system prompt
    std::string sys_json = "{\"role\":\"system\",\"content\":[{\"type\":\"text\",\"text\":\""
                          + json_utils::json_escape(system_prompt) + "\"}]}";
    litert_lm_conversation_config_set_system_message(conv_cfg, sys_json.c_str());

    // Enable thinking mode
    if (!cfg.no_thinking) {
        litert_lm_conversation_config_set_extra_context(conv_cfg,
            "{\"enable_thinking\": true}");
    }

    // Set tools if search enabled
    if (cfg.enable_search) {
        std::string tools = tavily_search::get_tool_definition();
        litert_lm_conversation_config_set_tools(conv_cfg, tools.c_str());
    }

    // Set history messages if any
    if (!history.empty()) {
        std::ostringstream msgs_json;
        msgs_json << "[";
        for (size_t i = 0; i < history.size(); i++) {
            if (i > 0) msgs_json << ",";
            msgs_json << "{\"role\":\"" << history[i].role << "\","
                      << "\"content\":[{\"type\":\"text\",\"text\":\""
                      << json_utils::json_escape(history[i].content)
                      << "\"}]}";
        }
        msgs_json << "]";
        litert_lm_conversation_config_set_messages(conv_cfg, msgs_json.str().c_str());
    }

    LiteRtLmConversation* conversation = litert_lm_conversation_create(engine, conv_cfg);
    litert_lm_conversation_config_delete(conv_cfg);

    return conversation;
}

// ---- Chat loop ----
static void chat_loop(const Config& cfg) {
    // Suppress verbose LiteRT-LM logging
    litert_lm_set_min_log_level(3); // WARNING level

    // ---- Create engine settings ----
    const char* vision_be = cfg.vision_backend.empty() ? "cpu"
                                                       : cfg.vision_backend.c_str();
    const char* audio_be = cfg.audio_backend.empty() ? "cpu"
                                                     : cfg.audio_backend.c_str();

    LiteRtLmEngineSettings* engine_settings = litert_lm_engine_settings_create(
        cfg.model_path.c_str(),
        cfg.backend.c_str(),
        vision_be,
        audio_be);

    if (!engine_settings) {
        cprintln("❌ Failed to create engine settings.", Color::Red);
        return;
    }

    // Configure engine settings
    if (cfg.max_num_tokens > 0) {
        litert_lm_engine_settings_set_max_num_tokens(engine_settings, cfg.max_num_tokens);
    }
    if (!cfg.cache_dir.empty()) {
        litert_lm_engine_settings_set_cache_dir(engine_settings, cfg.cache_dir.c_str());
    }
    if (cfg.speculative_decoding) {
        litert_lm_engine_settings_set_enable_speculative_decoding(engine_settings, true);
    }

    // ---- Create engine ----
    cprint("Loading model", Color::Cyan);
    cprint("...", Color::Dim);
    std::cout.flush();

    std::string active_backend = cfg.backend;
    LiteRtLmEngine* engine = litert_lm_engine_create(engine_settings);
    litert_lm_engine_settings_delete(engine_settings);

    if (!engine) {
        // If GPU backend failed, silently retry with CPU
        if (active_backend != "cpu") {
            std::cout << '\n';
            cprintln("⚠️  GPU unavailable, falling back to CPU", Color::Dim);
            active_backend = "cpu";
            engine_settings = litert_lm_engine_settings_create(
                cfg.model_path.c_str(), "cpu", vision_be, audio_be);
            if (cfg.max_num_tokens > 0) {
                litert_lm_engine_settings_set_max_num_tokens(engine_settings, cfg.max_num_tokens);
            }
            if (!cfg.cache_dir.empty()) {
                litert_lm_engine_settings_set_cache_dir(engine_settings, cfg.cache_dir.c_str());
            }
            if (cfg.speculative_decoding) {
                litert_lm_engine_settings_set_enable_speculative_decoding(engine_settings, true);
            }
            cprint("Loading model", Color::Cyan);
            cprint("...", Color::Dim);
            std::cout.flush();
            engine = litert_lm_engine_create(engine_settings);
            litert_lm_engine_settings_delete(engine_settings);
        }
        if (!engine) {
            std::cout << '\n';
            cprintln("❌ Failed to create engine. Check model path and backend.", Color::Red);
            return;
        }
    }

    std::cout << '\n';
    cprintln("✅ Model loaded successfully!", Color::Green);

    // ---- Read system prompt ----
    std::string system_prompt = read_system_prompt(cfg);

    // ---- Create initial conversation ----
    LiteRtLmConversation* conversation = create_conversation_with_history(
        engine, cfg, system_prompt, {});
    if (!conversation) {
        cprintln("❌ Failed to create conversation.", Color::Red);
        litert_lm_engine_delete(engine);
        return;
    }    // ---- Open conversation database (ensure parent dirs exist first) ----
    std::string db_path = conversation_db::ConversationDB::default_path();
    {
        std::error_code ec;
        fs::create_directories(fs::path(db_path).parent_path(), ec);
    }
    conversation_db::ConversationDB db(db_path);
    if (!db.is_open() || !db.init()) {
        if (cfg.debug) cprintln("⚠️  Failed to open conversation database (non-fatal)", Color::Dim);
    }

    // Start a new conversation in DB
    std::string conv_title;
    int64_t current_conv_id = -1;
    if (db.is_open()) {
        current_conv_id = db.create_conversation(cfg.model_path, active_backend, "");
    }

    // ---- Debug info ----
    if (cfg.debug) {
        print_debug_info(cfg, cfg.model_path, system_prompt);
    }

    // Register signal handler
    std::signal(SIGINT, sigint_handler);

    // Suppress noisy library stderr after model load unless debugging
    if (!cfg.debug) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
    }

    // ---- Chat loop ----
    std::string line;
    std::string audio_file = "/tmp/bb_recording.wav";

    while (true) {
        std::string current_audio;

        if (cfg.voice_mode) {
            // ---- Voice-only mode ----
            std::cout.flush();

            if (g_cancellation_requested.exchange(false, std::memory_order_acq_rel)) {
                std::cout << '\n';
                cprintln("⚠️  Cancelled.", Color::Yellow);
                continue;
            }

            if (!std::getline(std::cin, line)) break; // Ctrl+D

            // Trim and check for commands
            auto start = line.find_first_not_of(" \t\n\r");
            if (start != std::string::npos) {
                line = line.substr(start, line.find_last_not_of(" \t\n\r") - start + 1);
            if (line == "/exit" || line == "/quit") break;
            if (line == "/help") {
                cprintln("Voice mode: press ENTER to record. Commands: /exit, /quit, /model, /resume, /delete, /clear, /help", Color::Dim);
                continue;
            }
            if (line == "/model") {
                cprintln("Model: " + cfg.model_path, Color::BrightWhite);
                cprintln("Backend: " + active_backend, Color::BrightWhite);
                cprintln("Thinking: " + std::string(cfg.no_thinking ? "off" : "on"), Color::BrightWhite);
                cprintln("Search: " + std::string(cfg.enable_search ? "enabled" : "disabled"), Color::BrightWhite);
                continue;
            }
            if (line == "/delete") {
                cprintln("⚠️  /delete is only available in text mode. Use text mode to delete conversations.", Color::Yellow);
                continue;
            }
            if (line == "/resume") {
                cprintln("⚠️  /resume is only available in text mode. Use text mode to resume conversations.", Color::Yellow);
                continue;
            }
            if (line == "/clear") {
                litert_lm_conversation_delete(conversation);
                conversation = create_conversation_with_history(engine, cfg, system_prompt, {});
                if (!conversation) {
                    cprintln("❌ Failed to create new conversation.", Color::Red);
                    break;
                }
                if (db.is_open()) {
                    current_conv_id = db.create_conversation(cfg.model_path, active_backend, "");
                    conv_title.clear();
                }
                cprintln("✅ Conversation cleared.", Color::Green);
                continue;
            }
            }

            // Start recording
            std::filesystem::remove(audio_file);
            if (!audio_recorder::record_to_file(audio_file, true, 16000, 1, 120)) {
                continue;
            }
            current_audio = audio_file;
            line.clear();

        } else {
            // ---- Text mode ----
            cprint(">>> ", Color::BoldGreen);
            std::cout.flush();

            if (g_cancellation_requested.exchange(false, std::memory_order_acq_rel)) {
                std::cout << '\n';
                cprintln("⚠️  Cancelled.", Color::Yellow);
                continue;
            }

            if (!std::getline(std::cin, line)) break; // Ctrl+D

            // Trim
            auto start = line.find_first_not_of(" \t\n\r");
            auto end = line.find_last_not_of(" \t\n\r");
            if (start == std::string::npos) continue; // Empty line
            line = line.substr(start, end - start + 1);

            // ---- Handle commands ----
            if (line == "/exit" || line == "/quit") break;
            if (line == "/help") {
                cprintln("Commands: /exit, /quit, /clear, /model, /resume, /delete, /help", Color::Dim);
                continue;
            }
            if (line == "/model") {
                cprintln("Model: " + cfg.model_path, Color::BrightWhite);
                cprintln("Backend: " + active_backend, Color::BrightWhite);
                cprintln("Thinking: " + std::string(cfg.no_thinking ? "off" : "on"), Color::BrightWhite);
                cprintln("Search: " + std::string(cfg.enable_search ? "enabled" : "disabled"), Color::BrightWhite);
                continue;
            }
            if (line == "/clear") {
                // Reset conversation: delete old, create fresh
                litert_lm_conversation_delete(conversation);
                conversation = create_conversation_with_history(engine, cfg, system_prompt, {});
                if (!conversation) {
                    cprintln("❌ Failed to create new conversation.", Color::Red);
                    break;
                }
                // Start a new conversation in DB
                if (db.is_open()) {
                    current_conv_id = db.create_conversation(cfg.model_path, active_backend, "");
                    conv_title.clear();
                }
                cprintln("✅ Conversation cleared.", Color::Green);
                continue;
            }
            if (line == "/resume") {
                if (!db.is_open()) {
                    cprintln("⚠️  Conversation database not available.", Color::Yellow);
                    continue;
                }

                auto conversations = db.list_conversations();
                if (conversations.empty()) {
                    cprintln("No saved conversations found.", Color::Dim);
                    continue;
                }

                cprintln("Saved conversations:", Color::BrightWhite);
                for (size_t i = 0; i < conversations.size(); i++) {
                    std::string label = conversations[i].title.empty()
                        ? "(untitled)"
                        : conversations[i].title;
                    if (label.length() > 60) {
                        label = label.substr(0, 57) + "...";
                    }
                    cprintln("  " + std::to_string(i + 1) + ". " + label, Color::BrightWhite);
                    cprintln("     " + conversations[i].created_at + " | " +
                             conversations[i].model_path + " | " +
                             conversations[i].backend, Color::Dim);
                }

                cprint("Select conversation (1-" + std::to_string(conversations.size()) +
                       ") or 0 to cancel: ", Color::BoldGreen);
                std::cout.flush();

                std::string choice_str;
                if (!std::getline(std::cin, choice_str)) break;
                int choice;
                try { choice = std::stoi(choice_str); }
                catch (...) { cprintln("Invalid choice.", Color::Yellow); continue; }

                if (choice == 0) {
                    cprintln("Cancelled.", Color::Dim);
                    continue;
                }
                if (choice < 1 || choice > static_cast<int>(conversations.size())) {
                    cprintln("Invalid choice.", Color::Yellow);
                    continue;
                }

                auto& selected = conversations[static_cast<size_t>(choice - 1)];
                auto history = db.get_messages(selected.id);

                cprintln("Resuming conversation: " +
                         (selected.title.empty() ? "(untitled)" : selected.title),
                         Color::Cyan);

                // Rebuild conversation with history
                litert_lm_conversation_delete(conversation);
                conversation = create_conversation_with_history(engine, cfg, system_prompt, history);
                if (!conversation) {
                    cprintln("❌ Failed to resume conversation.", Color::Red);
                    break;
                }
                current_conv_id = selected.id;
                conv_title = selected.title;
                continue;
            }
            if (line == "/delete") {
                if (!db.is_open()) {
                    cprintln("⚠️  Conversation database not available.", Color::Yellow);
                    continue;
                }

                auto conversations = db.list_conversations();
                if (conversations.empty()) {
                    cprintln("No saved conversations to delete.", Color::Dim);
                    continue;
                }

                cprintln("Saved conversations:", Color::BrightWhite);
                for (size_t i = 0; i < conversations.size(); i++) {
                    std::string label = conversations[i].title.empty()
                        ? "(untitled)"
                        : conversations[i].title;
                    if (label.length() > 60) {
                        label = label.substr(0, 57) + "...";
                    }
                    cprintln("  " + std::to_string(i + 1) + ". " + label, Color::BrightWhite);
                    cprintln("     " + conversations[i].created_at + " | " +
                             conversations[i].model_path + " | " +
                             conversations[i].backend, Color::Dim);
                }

                cprint("Delete conversation (1-" + std::to_string(conversations.size()) +
                       ") or 0 to cancel: ", Color::BoldGreen);
                std::cout.flush();

                std::string choice_str;
                if (!std::getline(std::cin, choice_str)) break;
                int choice;
                try { choice = std::stoi(choice_str); }
                catch (...) { cprintln("Invalid choice.", Color::Yellow); continue; }

                if (choice == 0) {
                    cprintln("Cancelled.", Color::Dim);
                    continue;
                }
                if (choice < 1 || choice > static_cast<int>(conversations.size())) {
                    cprintln("Invalid choice.", Color::Yellow);
                    continue;
                }

                auto& selected = conversations[static_cast<size_t>(choice - 1)];
                std::string label = selected.title.empty() ? "(untitled)" : selected.title;

                if (db.delete_conversation(selected.id)) {
                    cprintln("🗑️  Deleted: " + label, Color::Green);
                    // If we deleted the current conversation, start a fresh one
                    if (selected.id == current_conv_id) {
                        litert_lm_conversation_delete(conversation);
                        conversation = create_conversation_with_history(engine, cfg, system_prompt, {});
                        if (!conversation) {
                            cprintln("❌ Failed to create new conversation.", Color::Red);
                            break;
                        }
                        current_conv_id = db.create_conversation(cfg.model_path, active_backend, "");
                        conv_title.clear();
                        cprintln("✅ Started fresh conversation.", Color::Green);
                    }
                } else {
                    cprintln("❌ Failed to delete conversation.", Color::Red);
                }
                continue;
            }
        }

        // Handle image/video attachment
        std::string current_image;
        if (!cfg.image_path.empty()) {
            current_image = cfg.image_path;
        }

        // Build message and save user input to DB
        std::string msg_json = build_message_json(line, current_image, current_audio);
        std::string user_text = line.empty() ? "[voice message]" : line;

        if (db.is_open() && current_conv_id >= 0) {
            bool ok = db.save_message(current_conv_id, "user", user_text);
            if (!ok && cfg.debug) cprintln("⚠️  Failed to save user message to DB", Color::Dim);
            // Update title from first user message
            if (conv_title.empty() && !line.empty()) {
                conv_title = line.length() > 80 ? line.substr(0, 77) + "..." : line;
                db.update_title(current_conv_id, conv_title);
            }
        }

        if (cfg.no_stream) {
            // ---- Non-streaming with automatic tool calling ----
            std::string current_msg = msg_json;
            const int MAX_TOOL_ROUNDS = 5;

            for (int round = 0; round < MAX_TOOL_ROUNDS; round++) {
                LiteRtLmJsonResponse* response =
                    litert_lm_conversation_send_message(conversation,
                                                        current_msg.c_str(),
                                                        "{}", nullptr);

                if (!response) {
                    if (round == 0) {
                        cprintln("❌ No response received.", Color::Red);
                    }
                    break;
                }

                const char* text = litert_lm_json_response_get_string(response);
                if (!text) {
                    litert_lm_json_response_delete(response);
                    break;
                }

                std::string resp_str(text);

                // Check for tool calls
                auto tool_calls = extract_tool_calls(resp_str);
                if (!tool_calls.empty() && cfg.enable_search) {
                    std::string tool_responses =
                        execute_tool_calls(tool_calls, cfg.tavily_api_key);
                    litert_lm_json_response_delete(response);
                    current_msg = tool_responses;
                    continue;
                }

                // No tool calls — print and save
                display_full_response(resp_str);
                if (db.is_open() && current_conv_id >= 0) {
                    std::string response_text = extract_response_text(resp_str);
                    if (!response_text.empty()) {
                        bool ok = db.save_message(current_conv_id, "assistant", response_text);
                        if (!ok && cfg.debug) cprintln("⚠️  Failed to save assistant message to DB", Color::Dim);
                    }
                }
                litert_lm_json_response_delete(response);
                break;
            }
        } else {
            // ---- Streaming with automatic tool calling ----
            std::string current_msg = msg_json;
            const int MAX_TOOL_ROUNDS = 5;

            for (int round = 0; round < MAX_TOOL_ROUNDS; round++) {
                StreamState state;
                int result = litert_lm_conversation_send_message_stream(
                    conversation,
                    current_msg.c_str(),
                    "{}", nullptr,
                    stream_callback,
                    &state);

                if (result != 0) {
                    cprintln("❌ Failed to send message (stream error).", Color::Red);
                    break;
                }

                // Wait for the background callback to finish
                {
                    std::unique_lock<std::mutex> lock(state.mtx);
                    constexpr auto poll_interval = std::chrono::milliseconds(100);
                    const auto deadline = std::chrono::steady_clock::now()
                                          + std::chrono::seconds(300);
                    bool cancelled = false;
                    while (!state.done && !cancelled) {
                        if (state.cv.wait_for(lock, poll_interval,
                                              [&state] { return state.done; })) {
                            break;
                        }
                        if (g_cancellation_requested.exchange(false,
                                                              std::memory_order_acq_rel)) {
                            cancelled = true;
                        }
                        if (std::chrono::steady_clock::now() >= deadline) {
                            break;
                        }
                    }
                    if (cancelled) {
                        lock.unlock();
                        cprintln("⚠️  Generation cancelled.", Color::Yellow);
                        litert_lm_conversation_cancel_process(conversation);
                        lock.lock();
                        state.cv.wait_for(lock, std::chrono::seconds(5),
                                          [&state] { return state.done; });
                        break;
                    }
                    if (!state.done) {
                        lock.unlock();
                        cprintln("❌ Streaming timed out (5 min).", Color::Red);
                        litert_lm_conversation_cancel_process(conversation);
                        lock.lock();
                        state.cv.wait_for(lock, std::chrono::seconds(5),
                                          [&state] { return state.done; });
                        break;
                    }
                }

                if (state.printed_any) {
                    std::cout.flush();
                }

                // Check for tool calls
                if (cfg.enable_search && !state.accumulated.empty()) {
                    auto tool_calls = extract_tool_calls(state.accumulated);
                    if (!tool_calls.empty()) {
                        std::string tool_responses =
                            execute_tool_calls(tool_calls, cfg.tavily_api_key);
                        current_msg = tool_responses;
                        continue;
                    }
                }

                // Save assistant response to DB
                if (db.is_open() && current_conv_id >= 0 && !state.accumulated.empty()) {
                    std::string response_text = extract_response_text(state.accumulated);
                    if (!response_text.empty()) {
                        bool ok = db.save_message(current_conv_id, "assistant", response_text);
                        if (!ok && cfg.debug) cprintln("⚠️  Failed to save assistant message to DB", Color::Dim);
                    }
                }
                break;
            }
        }

        std::cout << '\n';
    }

    // ---- Cleanup ----
    std::signal(SIGINT, SIG_DFL);
    litert_lm_conversation_delete(conversation);
    litert_lm_engine_delete(engine);

    std::error_code ec;
    std::filesystem::remove(audio_file, ec);

    std::cout << '\n';
    cprintln("👋 Goodbye!", Color::BoldWhite);
}

// ---- Main ----
int main(int argc, char* argv[]) {
    Config cfg = parse_args(argc, argv);

    // Default model: Gemma 4 E2B via LiteRT-LM community repo
    if (cfg.model_path.empty()) {
        cfg.model_path = "litert-community/gemma-4-E2B-it-litert-lm";
        cprintln("📦 No model specified, defaulting to: " + cfg.model_path, Color::Dim);
    }

    // Validate numeric parameters
    if (cfg.max_output_tokens <= 0) {
        std::cerr << "Error: --max-tokens must be positive.\n";
        return 1;
    }
    if (cfg.top_k < 0) {
        std::cerr << "Error: --top-k must be non-negative.\n";
        return 1;
    }
    if (cfg.top_p < 0.0f || cfg.top_p > 1.0f) {
        std::cerr << "Error: --top-p must be between 0.0 and 1.0.\n";
        return 1;
    }
    if (cfg.temperature < 0.0f) {
        std::cerr << "Error: --temperature must be non-negative.\n";
        return 1;
    }

    // Download only mode
    if (cfg.download_only) {
        return download_model(cfg) ? 0 : 1;
    }

    // Resolve model path: look for actual model file
    std::string resolved = resolve_model_path(cfg.model_path);
    if (!resolved.empty()) {
        if (resolved != cfg.model_path) {
            cprintln("📂 Resolved model: " + resolved, Color::Dim);
        }
        cfg.model_path = resolved;
    } else if (fs::is_directory(cfg.model_path)) {
        cprintln("❌ No model file (.litertlm or .safetensors) found in: " + cfg.model_path, Color::Red);
        cprintln("   Try passing the path to the .litertlm file directly.", Color::Dim);
        return 1;
    } else if (!fs::exists(cfg.model_path)) {
        std::string cache = cfg.cache_dir.empty()
            ? model_downloader::default_cache_dir()
            : cfg.cache_dir;
        std::string cached = cache + "/" + cfg.model_path;
        std::replace(cached.begin(), cached.end(), '/', '_');

        std::string hf_cache = std::string(std::getenv("HOME") ? std::getenv("HOME") : ".") +
                               "/.cache/huggingface/hub/models--litert-community--gemma-4-E2B-it-litert-lm";

        std::string cached_resolved = resolve_model_path(cached);
        if (!cached_resolved.empty()) {
            cfg.model_path = cached_resolved;
            cprintln("📂 Using cached model: " + cached_resolved, Color::Dim);
        } else {
            std::string hf_resolved = resolve_model_path(hf_cache);
            if (!hf_resolved.empty()) {
                cfg.model_path = hf_resolved;
                cprintln("📂 Found model in HuggingFace cache: " + hf_resolved, Color::Dim);
            } else {
                cprintln("📥 Model not found locally. Auto-downloading from HuggingFace...", Color::Cyan);
                cprintln("   (this may take a few minutes — press Ctrl+C to cancel)", Color::Dim);

                const char* python_cmd = nullptr;
                for (auto* candidate : {"python3", "python"}) {
                    std::string check = std::string(candidate) + " -c \"import litert_lm\" 2>/dev/null";
                    if (std::system(check.c_str()) == 0) {
                        python_cmd = candidate;
                        break;
                    }
                }
                if (!python_cmd) python_cmd = "python3";

                std::string dl_cmd = std::string(python_cmd) +
                    " -m litert_lm run --from-huggingface-repo=litert-community/gemma-4-E2B-it-litert-lm gemma-4-E2B-it 2>&1";
                int dl_result = std::system(dl_cmd.c_str());
                if (dl_result != 0) {
                    cprintln("❌ Auto-download failed. Try manually:\n"
                            "   pip install litert-lm huggingface_hub\n"
                            "   python3 -m litert_lm run --from-huggingface-repo=litert-community/gemma-4-E2B-it-litert-lm gemma-4-E2B-it",
                            Color::Red);
                    return 1;
                }

                hf_resolved = resolve_model_path(hf_cache);
                if (!hf_resolved.empty()) {
                    cfg.model_path = hf_resolved;
                    cprintln("✅ Model downloaded: " + hf_resolved, Color::Green);
                } else {
                    std::string litert_cache = std::string(std::getenv("HOME") ? std::getenv("HOME") : ".") +
                                               "/.cache/litert_lm/models--litert-community--gemma-4-E2B-it-litert-lm";
                    std::string litert_resolved = resolve_model_path(litert_cache);
                    if (!litert_resolved.empty()) {
                        cfg.model_path = litert_resolved;
                        cprintln("✅ Model downloaded: " + litert_resolved, Color::Green);
                    } else {
                        cprintln("❌ Auto-download failed. Try manually:\n"
                                "   python3 -m litert_lm run --from-huggingface-repo=litert-community/gemma-4-E2B-it-litert-lm gemma-4-E2B-it",
                                Color::Red);
                        return 1;
                    }
                }
            }
        }
    }

    // Start chat
    chat_loop(cfg);
    return 0;
}
