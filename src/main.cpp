// Tiny-Habibi - C++ CLI for running Gemma models with LiteRT-LM
// Features: streaming chat, voice input, image/video input,
//            Tavily web search, model download, GPU backend

#include "litert_lm_c_api.h"
#include "terminal.hpp"
#include "model_downloader.hpp"
#include "audio_recorder.hpp"
#include "tavily_search.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
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
    std::cout << R"(Tiny-Habibi - Interactive CLI for Gemma models via LiteRT-LM

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
  --tavily-key KEY       Tavily API key (or set TAVILY_API_KEY env var)

System Options:
  --system-prompt PATH   Path to system prompt file (default: AGENTS.md in CWD)
  --debug                Print debug info (model, system prompt, chat template, etc.)
  --help, -h             Show this help message

Environment Variables:
  TAVILY_API_KEY         Default Tavily API key for web search

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

    // Check TAVILY_API_KEY env var
    const char* env_key = std::getenv("TAVILY_API_KEY");
    if (env_key) cfg.tavily_api_key = env_key;

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
        else if (arg == "--tavily-key")           cfg.tavily_api_key = next();
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

// ---- Escape string for JSON ----
static std::string json_escape(std::string_view str) {
    std::string out;
    // Reserve headroom to avoid repeated reallocations.
    out.reserve(str.length() * 2);
    for (char c : str) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                // Control character: escape as \u00XX
                out += "\\u00";
                out += "0123456789abcdef"[(c >> 4) & 0xf];
                out += "0123456789abcdef"[c & 0xf];
            } else {
                out += c;
            }
        }
    }
    return out;
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
             << json_escape(text) << "\"}";
        first = false;
    }

    // Image part
    if (!image_path.empty()) {
        if (!first) json << ",";
        json << "{\"type\":\"image\",\"path\":\""
             << json_escape(image_path) << "\"}";
        first = false;
    }

    // Audio part
    if (!audio_path.empty()) {
        if (!first) json << ",";
        json << "{\"type\":\"audio\",\"path\":\""
             << json_escape(audio_path) << "\"}";
        first = false;
    }

    json << "]}";

    return json.str();
}

// ---- Minimal JSON string extractor ----
static std::string json_get_str(std::string_view json, std::string_view key) {
    std::string search = std::string("\"") + std::string(key) + "\"";
    auto pos = json.find(search);
    if (pos == std::string_view::npos) return "";
    auto colon = json.find(':', pos + search.length());
    if (colon == std::string_view::npos) return "";
    auto open = json.find('"', colon + 1);
    if (open == std::string_view::npos) return "";
    auto close = json.find('"', open + 1);
    if (close == std::string_view::npos) return "";
    return std::string(json.substr(open + 1, close - open - 1));
}

// ---- Extract raw JSON object after a key (for nested objects) ----
static std::string json_get_object(std::string_view json, std::string_view key) {
    std::string search = std::string("\"") + std::string(key) + "\"";
    auto pos = json.find(search);
    if (pos == std::string_view::npos) return "";
    auto colon = json.find(':', pos + search.length());
    if (colon == std::string_view::npos) return "";

    // Skip whitespace after colon
    size_t start = colon + 1;
    while (start < json.length() && (json[start] == ' ' || json[start] == '\t' || json[start] == '\n'))
        start++;

    if (start >= json.length() || json[start] != '{') return "";

    // Find matching close brace
    int depth = 0;
    for (size_t i = start; i < json.length(); i++) {
        if (json[i] == '{') depth++;
        else if (json[i] == '}') {
            depth--;
            if (depth == 0) return std::string(json.substr(start, i - start + 1));
        }
        if (json[i] == '"') {
            i++;
            while (i < json.length() && json[i] != '"') {
                if (json[i] == '\\') i++;
                i++;
            }
        }
    }
    return "";
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
                    tc.name = json_get_str(func, "name");
                    tc.arguments_json = json_get_object(func, "arguments");
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
            std::string query = json_get_str(calls[i].arguments_json, "query");
            if (query.empty()) {
                query = json_get_str(calls[i].arguments_json, "q");
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
                     << "\"name\":\"" << json_escape(calls[i].name) << "\","
                     << "\"response\":\"Error: Unknown tool \\\"" << json_escape(calls[i].name) << "\\\"\""
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
    std::string accumulated;       // accumulate full JSON for tool detection
    std::string active_channel;    // track current channel name
};

// Extract raw JSON array after a key
static std::string json_get_array(std::string_view json, std::string_view key) {
    std::string search = std::string("\"") + std::string(key) + "\"";
    auto pos = json.find(search);
    if (pos == std::string_view::npos) return "";
    auto colon = json.find(':', pos + search.length());
    if (colon == std::string_view::npos) return "";

    size_t start = colon + 1;
    while (start < json.length() && (json[start] == ' ' || json[start] == '\t' || json[start] == '\n'))
        start++;

    if (start >= json.length() || json[start] != '[') return "";

    int depth = 0;
    for (size_t i = start; i < json.length(); i++) {
        if (json[i] == '[') depth++;
        else if (json[i] == ']') {
            depth--;
            if (depth == 0) return std::string(json.substr(start, i - start + 1));
        }
        if (json[i] == '"') {
            i++;
            while (i < json.length() && json[i] != '"') {
                if (json[i] == '\\') i++;
                i++;
            }
        }
    }
    return "";
}

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
                    std::string text(obj.substr(text_start, text_end - text_start));
                    if (!text.empty()) {
                        on_text(text);
                    }
                }
            }
        }

        search = obj_end + 1;
    }
}
// Channel content (e.g., thinking) is displayed in yellow
// Text content is displayed in green
static void display_chunk(std::string_view chunk, StreamState& state) {
    if (chunk.empty()) return;

    // Non-JSON chunks: print as plain green text (fallback)
    if (chunk[0] != '{' && chunk[0] != '[') {
        if (!state.active_channel.empty()) {
            std::cout << '\n';
            state.active_channel.clear();
        }
        cprint(chunk, Color::Green);
        state.printed_any = true;
        return;
    }

    // Parse channels: {"channels":{"channel_name":"content",...}}
    std::string channels_obj = json_get_object(chunk, "channels");
    if (!channels_obj.empty()) {
        size_t pos = 1;  // skip opening {
        while (pos < channels_obj.length()) {
            auto kq = channels_obj.find('"', pos);
            if (kq == std::string::npos) break;
            auto kqe = channels_obj.find('"', kq + 1);
            if (kqe == std::string::npos) break;
            std::string ch_name(channels_obj.substr(kq + 1, kqe - kq - 1));

            auto colon = channels_obj.find(':', kqe);
            if (colon == std::string::npos) break;
            auto vq = channels_obj.find('"', colon + 1);
            if (vq == std::string::npos) break;
            auto vqe = channels_obj.find('"', vq + 1);
            if (vqe == std::string::npos) break;
            std::string ch_content(channels_obj.substr(vq + 1, vqe - vq - 1));

            if (!ch_content.empty()) {
                if (ch_name != state.active_channel) {
                    if (state.printed_any) std::cout << '\n';
                    state.active_channel = ch_name;
                }
                cprint(ch_content, Color::Yellow);
                state.printed_any = true;
            }

            pos = vqe + 1;
        }
    }

    // Show tool call indicator if chunk contains tool_calls
    if (chunk.find("\"tool_calls\"") != std::string_view::npos) {
        if (!state.active_channel.empty()) {
            std::cout << '\n';
            state.active_channel.clear();
        }
        if (state.printed_any) std::cout << '\n';
        cprint("🔧 Tool call requested...", Color::Cyan);
        state.printed_any = true;
    }

    // Parse text from content array: {"content":[{"type":"text","text":"..."}]}
    std::string content_arr = json_get_array(chunk, "content");
    if (!content_arr.empty()) {
        parse_content_array(content_arr, [&](const std::string& text) {
            if (!state.active_channel.empty()) {
                std::cout << '\n';
                state.active_channel.clear();
            }
            cprint(text, Color::Green);
            state.printed_any = true;
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
            // Close active channel if any
            if (!state->active_channel.empty()) {
                state->active_channel.clear();
            }
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
    // HuggingFace API: https://huggingface.co/{model_id}/resolve/main/
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

        // Also need tokenizer and config
        // For now, just try to download the model file
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
            // Update model path to the downloaded file
            return true;
        }
    }

    cprintln("❌ Failed to download model. Check the model ID and your internet connection.",
            Color::Red);
    return false;
}

// Display a complete (non-streaming) JSON response with channel/text parsing
static void display_full_response(std::string_view json) {
    if (json.empty()) return;

    // Parse channels: {"channels":{"channel_name":"content",...}}
    std::string channels_obj = json_get_object(json, "channels");
    if (!channels_obj.empty()) {
        size_t pos = 1;  // skip opening {
        while (pos < channels_obj.length()) {
            auto kq = channels_obj.find('"', pos);
            if (kq == std::string::npos) break;
            auto kqe = channels_obj.find('"', kq + 1);
            if (kqe == std::string::npos) break;

            auto colon = channels_obj.find(':', kqe);
            if (colon == std::string::npos) break;
            auto vq = channels_obj.find('"', colon + 1);
            if (vq == std::string::npos) break;
            auto vqe = channels_obj.find('"', vq + 1);
            if (vqe == std::string::npos) break;
            std::string ch_content(channels_obj.substr(vq + 1, vqe - vq - 1));

            if (!ch_content.empty()) {
                // Unescape JSON \\n, \\t, \\" etc. to real characters for display.
                // Build a new string rather than modifying in-place (avoid index shifting bugs).
                std::string unescaped;
                unescaped.reserve(ch_content.length());
                for (size_t i = 0; i < ch_content.length(); i++) {
                    if (ch_content[i] == '\\' && i + 1 < ch_content.length()) {
                        switch (ch_content[i + 1]) {
                        case 'n':  unescaped += '\n'; i++; break;
                        case 't':  unescaped += '\t'; i++; break;
                        case 'r':  unescaped += '\r'; i++; break;
                        case '"':  unescaped += '"'; i++; break;
                        case '\\': unescaped += '\\'; i++; break;
                        default:   unescaped += ch_content[i]; break;
                        }
                    } else {
                        unescaped += ch_content[i];
                    }
                }
                cprint(unescaped, Color::Yellow);
                std::cout << '\n';
            }

            pos = vqe + 1;
        }
    }

    // Show tool call indicator if response contains tool_calls
    if (json.find("\"tool_calls\"") != std::string_view::npos) {
        cprint("🔧 Tool call requested...", Color::Cyan);
        std::cout << '\n';
    }

    // Parse text from content array: {"content":[{"type":"text","text":"..."}]}
    std::string content_arr = json_get_array(json, "content");
    if (!content_arr.empty()) {
        parse_content_array(content_arr, [&](const std::string& text) {
            cprint(text, Color::Green);
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

// ---- Chat loop ----
static void chat_loop(const Config& cfg) {
    // Suppress verbose LiteRT-LM logging
    litert_lm_set_min_log_level(3); // WARNING level

    // ---- Create engine settings ----
    // Vision/audio backends default to "cpu" if not explicitly set,
    // since multimodal models often require CPU for encoders.
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

    LiteRtLmEngine* engine = litert_lm_engine_create(engine_settings);
    litert_lm_engine_settings_delete(engine_settings);

    if (!engine) {
        std::cout << '\n';
        cprintln("❌ Failed to create engine. Check model path and backend.", Color::Red);
        return;
    }

    std::cout << '\n';
    cprintln("✅ Model loaded successfully!", Color::Green);

    // ---- Create session config ----
    LiteRtLmSessionConfig* session_cfg = litert_lm_session_config_create();
    litert_lm_session_config_set_max_output_tokens(session_cfg, cfg.max_output_tokens);
    // Enable the Jinja chat template from model metadata (Gemma 4 uses it)
    litert_lm_session_config_set_apply_prompt_template(session_cfg, true);

    LiteRtLmSamplerParams sampler = {
        kLiteRtLmSamplerTypeTopP,
        cfg.top_k,
        cfg.top_p,
        cfg.temperature,
        cfg.seed,
    };
    litert_lm_session_config_set_sampler_params(session_cfg, &sampler);

    // ---- Create conversation config ----
    LiteRtLmConversationConfig* conv_cfg = litert_lm_conversation_config_create();
    litert_lm_conversation_config_set_session_config(conv_cfg, session_cfg);
    litert_lm_session_config_delete(session_cfg);

    // Set system prompt
    std::string system_prompt = read_system_prompt(cfg);
    std::string sys_json = "{\"role\":\"system\",\"content\":[{\"type\":\"text\",\"text\":\""
                          + json_escape(system_prompt) + "\"}]}";
    litert_lm_conversation_config_set_system_message(conv_cfg, sys_json.c_str());

    // Enable thinking mode (Gemma 4 E2B supports reasoning/thinking channels)
    if (!cfg.no_thinking) {
        litert_lm_conversation_config_set_extra_context(conv_cfg,
            "{\"enable_thinking\": true}");
    }

    // Set tools if search enabled
    if (cfg.enable_search) {
        std::string tools = tavily_search::get_tool_definition();
        litert_lm_conversation_config_set_tools(conv_cfg, tools.c_str());
    }

    // ---- Create conversation ----
    LiteRtLmConversation* conversation = litert_lm_conversation_create(engine, conv_cfg);
    litert_lm_conversation_config_delete(conv_cfg);

    if (!conversation) {
        cprintln("❌ Failed to create conversation.", Color::Red);
        litert_lm_engine_delete(engine);
        return;
    }

    // ---- Debug info ----
    if (cfg.debug) {
        print_debug_info(cfg, cfg.model_path, system_prompt);
    }

    // Register signal handler (async-signal-safe: only sets atomic flag;
    // actual cancellation/cout happens in the main loop below)
    std::signal(SIGINT, sigint_handler);

    // ---- Welcome ----
    std::cout << '\n';
    print_separator(Color::Dim);
    cprintln("Tiny-Habibi - Type your message (Ctrl+C to cancel, Ctrl+D to exit)",
            Color::BrightWhite);
    cprintln(std::string("Backend: ") + cfg.backend +
             (cfg.speculative_decoding ? " (speculative)" : "") +
             (cfg.enable_search ? " (search)" : "") +
             (cfg.voice_mode ? " (voice)" : ""),
             Color::Dim);
    print_separator(Color::Dim);
    std::cout << '\n';

    // ---- Chat loop ----
    std::string line;
    std::string audio_file = "/tmp/bb_recording.wav";

    while (true) {
        // Prompt
        cprint(">>> ", Color::BoldGreen);
        std::cout.flush();

        // Consume any stale Ctrl+C flag from idle time (nothing to cancel)
        if (g_cancellation_requested.exchange(false, std::memory_order_acq_rel)) {
            std::cout << '\n';
            cprintln("⚠️  Cancelled.", Color::Yellow);
            continue;
        }

        if (!std::getline(std::cin, line)) {
            break; // Ctrl+D
        }

        // Trim
        auto start = line.find_first_not_of(" \t\n\r");
        auto end = line.find_last_not_of(" \t\n\r");
        if (start == std::string::npos) continue; // Empty line
        line = line.substr(start, end - start + 1);

        // Handle special commands
        if (line == "/exit" || line == "/quit") break;
        if (line == "/help") {
            cprintln("Commands: /exit, /quit, /clear, /help", Color::Dim);
            continue;
        }

        // Voice recording if enabled
        std::string current_audio;
        if (cfg.voice_mode) {
            std::filesystem::remove(audio_file);
            if (audio_recorder::record_to_file(audio_file, true, 16000, 1, 120)) {
                current_audio = audio_file;
            }
        }

        // Handle image/video attachment (first turn only, or set via CLI arg)
        std::string current_image;
        if (!cfg.image_path.empty()) {
            current_image = cfg.image_path;
        }

        // Build message
        std::string msg_json = build_message_json(line, current_image, current_audio);

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
                    // Execute tool calls and send response back
                    std::string tool_responses =
                        execute_tool_calls(tool_calls, cfg.tavily_api_key);
                    litert_lm_json_response_delete(response);

                    // C API accepts JSON array of messages directly
                    current_msg = tool_responses;
                    continue;  // Loop to get model's final response
                }

                // No tool calls — print the response with channel/text formatting
                display_full_response(resp_str);
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

                // Wait for the background callback to finish (non-blocking API).
                // Poll in 100ms increments so Ctrl+C cancellation is detected promptly.
                {
                    std::unique_lock<std::mutex> lock(state.mtx);
                    constexpr auto poll_interval = std::chrono::milliseconds(100);
                    const auto deadline = std::chrono::steady_clock::now()
                                          + std::chrono::seconds(300);
                    bool cancelled = false;
                    while (!state.done && !cancelled) {
                        if (state.cv.wait_for(lock, poll_interval,
                                              [&state] { return state.done; })) {
                            break;  // done
                        }
                        // Check ctrl+c flag (set by async-signal-safe sigint_handler)
                        if (g_cancellation_requested.exchange(false,
                                                              std::memory_order_acq_rel)) {
                            cancelled = true;
                        }
                        if (std::chrono::steady_clock::now() >= deadline) {
                            break;  // full timeout
                        }
                    }
                    if (cancelled) {
                        lock.unlock();  // release before cancel_process (avoids deadlock
                                        // if dylib calls callback synchronously)
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

                // After streaming, check accumulated response for tool calls
                if (cfg.enable_search && !state.accumulated.empty()) {
                    auto tool_calls = extract_tool_calls(state.accumulated);
                    if (!tool_calls.empty()) {
                        std::string tool_responses =
                            execute_tool_calls(tool_calls, cfg.tavily_api_key);
                        current_msg = tool_responses;
                        continue;  // Loop to get model's response to tool results
                    }
                }
                break;  // No tool calls, done
            }
        }

        std::cout << '\n';
    }

    // ---- Cleanup ----
    // Restore default SIGINT handler
    std::signal(SIGINT, SIG_DFL);
    litert_lm_conversation_delete(conversation);
    litert_lm_engine_delete(engine);

    // Clean up temp audio file
    std::error_code ec;
    std::filesystem::remove(audio_file, ec);

    std::cout << '\n';
    cprintln("👋 Goodbye!", Color::BoldWhite);
}

// ---- Main ----
int main(int argc, char* argv[]) {
    Config cfg = parse_args(argc, argv);

    // Validate
    if (cfg.model_path.empty()) {
        std::cerr << "Error: --model is required.\n";
        std::cerr << "Try '" << argv[0] << " --help' for usage.\n";
        return 1;
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
        // Directory exists but no .litertlm or .safetensors file found
        cprintln("❌ No model file (.litertlm or .safetensors) found in: " + cfg.model_path, Color::Red);
        cprintln("   Try passing the path to the .litertlm file directly.", Color::Dim);
        return 1;
    } else if (!fs::exists(cfg.model_path)) {
        // Path doesn't exist at all - try cache
        std::string cache = cfg.cache_dir.empty()
            ? model_downloader::default_cache_dir()
            : cfg.cache_dir;
        std::string cached = cache + "/" + cfg.model_path;
        std::replace(cached.begin(), cached.end(), '/', '_');

        // Try to resolve from cache directory
        std::string cached_resolved = resolve_model_path(cached);
        if (!cached_resolved.empty()) {
            cfg.model_path = cached_resolved;
            cprintln("📂 Using cached model: " + cached_resolved, Color::Dim);
        } else {
            cprintln("⚠️  Model not found locally. Use --download to download first.",
                    Color::Yellow);
            return 1;
        }
    }

    // Start chat
    chat_loop(cfg);
    return 0;
}
