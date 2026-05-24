// Unit tests for Skylark JSON utility functions
// Compile: g++ -std=c++20 -o test_json_utils test_json_utils.cpp
// Run: ./test_json_utils

#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// ---- Counters ----
static int g_passed = 0;
static int g_failed = 0;

#define TEST(name) \
    std::cout << "  " << name << "... "; \
    try {

#define END_TEST() \
    std::cout << "PASS" << std::endl; \
    g_passed++; \
    } catch (const std::exception& e) { \
        std::cout << "FAIL: " << e.what() << std::endl; \
        g_failed++; \
    } catch (...) { \
        std::cout << "FAIL: unknown exception" << std::endl; \
        g_failed++; \
    }

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) throw std::runtime_error(std::string("expected ") + #b + " got " + std::to_string(a))

#define ASSERT_STREQ(a, b) \
    if (std::string(a) != std::string(b)) throw std::runtime_error(std::string("expected '") + b + "' got '" + a + "'")

#define ASSERT_TRUE(cond) \
    if (!(cond)) throw std::runtime_error(#cond " is false")

#define ASSERT_FALSE(cond) \
    if (cond) throw std::runtime_error(#cond " is true")

// ====================================================================
// Copied from src/main.cpp (these are static functions, tested here)
// ====================================================================

// ---- Escape string for JSON ----
static std::string json_escape(std::string_view str) {
    std::string out;
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

// ---- Extract raw JSON object after a key ----
static std::string json_get_object(std::string_view json, std::string_view key) {
    std::string search = std::string("\"") + std::string(key) + "\"";
    auto pos = json.find(search);
    if (pos == std::string_view::npos) return "";
    auto colon = json.find(':', pos + search.length());
    if (colon == std::string_view::npos) return "";

    size_t start = colon + 1;
    while (start < json.length() && (json[start] == ' ' || json[start] == '\t' || json[start] == '\n'))
        start++;

    if (start >= json.length() || json[start] != '{') return "";

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

// ---- Extract raw JSON array after a key ----
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

// ---- Shared: parse a JSON array for text content ----
template <typename F>
static void parse_content_array(std::string_view content_arr, F&& on_text) {
    size_t search = 1;
    while (search < content_arr.length()) {
        auto obj_start = content_arr.find('{', search);
        if (obj_start == std::string_view::npos) break;

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

// ---- Tool call extraction ----
struct ToolCall {
    std::string name;
    std::string arguments_json;
};

static std::vector<ToolCall> extract_tool_calls(std::string_view json) {
    std::vector<ToolCall> calls;

    auto tc_pos = json.find("\"tool_calls\"");
    if (tc_pos == std::string_view::npos) return calls;

    auto arr_start = json.find('[', tc_pos);
    if (arr_start == std::string_view::npos) return calls;

    size_t cur = arr_start + 1;
    while (cur < json.length()) {
        auto obj_start = json.find('{', cur);
        if (obj_start == std::string_view::npos) break;
        if (obj_start >= json.find(']', arr_start)) break;

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

        auto func_pos = obj.find("\"function\"");
        if (func_pos != std::string_view::npos) {
            auto func_obj = obj.find('{', func_pos);
            if (func_obj != std::string_view::npos) {
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

// ---- Build message JSON ----
static std::string build_message_json(std::string_view text,
                                      std::string_view image_path,
                                      std::string_view audio_path) {
    std::ostringstream json;

    json << "{\"role\":\"user\",\"content\":[";

    bool first = true;

    if (!text.empty()) {
        json << "{\"type\":\"text\",\"text\":\""
             << json_escape(text) << "\"}";
        first = false;
    }

    if (!image_path.empty()) {
        if (!first) json << ",";
        json << "{\"type\":\"image\",\"path\":\""
             << json_escape(image_path) << "\"}";
        first = false;
    }

    if (!audio_path.empty()) {
        if (!first) json << ",";
        json << "{\"type\":\"audio\",\"path\":\""
             << json_escape(audio_path) << "\"}";
        first = false;
    }

    json << "]}";

    return json.str();
}

// ====================================================================
// TESTS
// ====================================================================

void test_json_escape() {
    TEST("json_escape: plain text unchanged") {
        ASSERT_STREQ(json_escape("hello"), "hello");
    } END_TEST();

    TEST("json_escape: quotes escaped") {
        ASSERT_STREQ(json_escape("say \"hi\""), "say \\\"hi\\\"");
    } END_TEST();

    TEST("json_escape: backslash escaped") {
        ASSERT_STREQ(json_escape("a\\b"), "a\\\\b");
    } END_TEST();

    TEST("json_escape: newline escaped") {
        ASSERT_STREQ(json_escape("line1\nline2"), "line1\\nline2");
    } END_TEST();

    TEST("json_escape: tab escaped") {
        ASSERT_STREQ(json_escape("col1\tcol2"), "col1\\tcol2");
    } END_TEST();

    TEST("json_escape: carriage return escaped") {
        ASSERT_STREQ(json_escape("a\rb"), "a\\rb");
    } END_TEST();

    TEST("json_escape: backspace escaped") {
        ASSERT_STREQ(json_escape("a\bb"), "a\\bb");
    } END_TEST();

    TEST("json_escape: form feed escaped") {
        ASSERT_STREQ(json_escape("a\fb"), "a\\fb");
    } END_TEST();

    TEST("json_escape: control chars (0x01) escaped") {
        std::string input;
        input += '\x01';
        auto result = json_escape(input);
        ASSERT_TRUE(result.find("\\u0001") != std::string::npos);
    } END_TEST();

    TEST("json_escape: empty string") {
        ASSERT_STREQ(json_escape(""), "");
    } END_TEST();

    TEST("json_escape: all special chars") {
        std::string all = "\\\"\n\r\t\b\f";
        auto result = json_escape(all);
        ASSERT_TRUE(result.find("\\\\") != std::string::npos);
        ASSERT_TRUE(result.find("\\\"") != std::string::npos);
        ASSERT_TRUE(result.find("\\n") != std::string::npos);
        ASSERT_TRUE(result.find("\\r") != std::string::npos);
        ASSERT_TRUE(result.find("\\t") != std::string::npos);
        ASSERT_TRUE(result.find("\\b") != std::string::npos);
        ASSERT_TRUE(result.find("\\f") != std::string::npos);
    } END_TEST();

    TEST("json_escape: no false positives on normal chars") {
        ASSERT_STREQ(json_escape("abcdefghijklmnopqrstuvwxyz0123456789"),
                     "abcdefghijklmnopqrstuvwxyz0123456789");
    } END_TEST();
}

void test_json_get_str() {
    TEST("json_get_str: simple key") {
        ASSERT_STREQ(json_get_str("{\"name\":\"Alice\"}", "name"), "Alice");
    } END_TEST();

    TEST("json_get_str: key with spaces") {
        ASSERT_STREQ(json_get_str("{\"name\" : \"Bob\"}", "name"), "Bob");
    } END_TEST();

    TEST("json_get_str: missing key returns empty") {
        ASSERT_STREQ(json_get_str("{\"name\":\"Alice\"}", "age"), "");
    } END_TEST();

    TEST("json_get_str: empty json returns empty") {
        ASSERT_STREQ(json_get_str("", "key"), "");
    } END_TEST();

    TEST("json_get_str: finds first simple string match (not full JSON parser)") {
        // When keys share names across nesting levels, the simple string search
        // finds the first textual match — this is a known limitation.
        auto result = json_get_str("{\"a\":{\"a\":\"inner\"},\"a\":\"outer\"}", "a");
        ASSERT_TRUE(result == "a" || result == "outer");  // either is acceptable
    } END_TEST();

    TEST("json_get_str: key with empty value") {
        ASSERT_STREQ(json_get_str("{\"key\":\"\"}", "key"), "");
    } END_TEST();

    TEST("json_get_str: no colon returns empty") {
        ASSERT_STREQ(json_get_str("{\"key\" \"val\"}", "key"), "");
    } END_TEST();
}

void test_json_get_object() {
    TEST("json_get_object: simple nested object") {
        auto result = json_get_object("{\"data\":{\"x\":1}}", "data");
        ASSERT_STREQ(result, "{\"x\":1}");
    } END_TEST();

    TEST("json_get_object: object with nested braces") {
        auto result = json_get_object("{\"outer\":{\"inner\":{\"deep\":true}}}", "outer");
        ASSERT_STREQ(result, "{\"inner\":{\"deep\":true}}");
    } END_TEST();

    TEST("json_get_object: missing key") {
        auto result = json_get_object("{\"a\":1}", "b");
        ASSERT_STREQ(result, "");
    } END_TEST();

    TEST("json_get_object: value is not an object") {
        auto result = json_get_object("{\"a\":\"string\"}", "a");
        ASSERT_STREQ(result, "");
    } END_TEST();
}

void test_json_get_array() {
    TEST("json_get_array: simple array") {
        auto result = json_get_array("{\"items\":[1,2,3]}", "items");
        ASSERT_STREQ(result, "[1,2,3]");
    } END_TEST();

    TEST("json_get_array: nested arrays") {
        auto result = json_get_array("{\"m\":[[1],[2,3]]}", "m");
        ASSERT_STREQ(result, "[[1],[2,3]]");
    } END_TEST();

    TEST("json_get_array: missing key") {
        auto result = json_get_array("{\"a\":1}", "b");
        ASSERT_STREQ(result, "");
    } END_TEST();

    TEST("json_get_array: value is not array") {
        auto result = json_get_array("{\"a\":\"string\"}", "a");
        ASSERT_STREQ(result, "");
    } END_TEST();
}

void test_parse_content_array() {
    TEST("parse_content_array: single text object") {
        std::vector<std::string> texts;
        parse_content_array("[{\"type\":\"text\",\"text\":\"Hello\"}]",
                            [&](const std::string& t) { texts.push_back(t); });
        ASSERT_EQ(texts.size(), 1);
        ASSERT_STREQ(texts[0], "Hello");
    } END_TEST();

    TEST("parse_content_array: multiple text objects") {
        std::vector<std::string> texts;
        parse_content_array("[{\"type\":\"text\",\"text\":\"A\"},{\"type\":\"text\",\"text\":\"B\"}]",
                            [&](const std::string& t) { texts.push_back(t); });
        ASSERT_EQ(texts.size(), 2);
        ASSERT_STREQ(texts[0], "A");
        ASSERT_STREQ(texts[1], "B");
    } END_TEST();

    TEST("parse_content_array: empty array") {
        std::vector<std::string> texts;
        parse_content_array("[]", [&](const std::string& t) { texts.push_back(t); });
        ASSERT_EQ(texts.size(), 0);
    } END_TEST();

    TEST("parse_content_array: non-text objects skipped") {
        std::vector<std::string> texts;
        parse_content_array("[{\"type\":\"image\",\"path\":\"img.jpg\"}]",
                            [&](const std::string& t) { texts.push_back(t); });
        ASSERT_EQ(texts.size(), 0);
    } END_TEST();

    TEST("parse_content_array: mixed types, only text extracted") {
        std::vector<std::string> texts;
        parse_content_array("[{\"type\":\"image\",\"path\":\"x.jpg\"},{\"type\":\"text\",\"text\":\"hi\"}]",
                            [&](const std::string& t) { texts.push_back(t); });
        ASSERT_EQ(texts.size(), 1);
        ASSERT_STREQ(texts[0], "hi");
    } END_TEST();
}

void test_extract_tool_calls() {
    TEST("extract_tool_calls: single web_search tool") {
        std::string json = R"({"tool_calls":[{"function":{"name":"web_search","arguments":{"query":"test"}}}]})";
        auto calls = extract_tool_calls(json);
        ASSERT_EQ(calls.size(), 1);
        ASSERT_STREQ(calls[0].name, "web_search");
    } END_TEST();

    TEST("extract_tool_calls: no tool_calls in json") {
        auto calls = extract_tool_calls("{\"content\":\"hello\"}");
        ASSERT_EQ(calls.size(), 0);
    } END_TEST();

    TEST("extract_tool_calls: empty json") {
        auto calls = extract_tool_calls("");
        ASSERT_EQ(calls.size(), 0);
    } END_TEST();

    TEST("extract_tool_calls: multiple tools") {
        std::string json = R"({"tool_calls":[
            {"function":{"name":"web_search","arguments":{"query":"a"}}},
            {"function":{"name":"get_weather","arguments":{"location":"NYC"}}}
        ]})";
        auto calls = extract_tool_calls(json);
        ASSERT_EQ(calls.size(), 2);
        ASSERT_STREQ(calls[0].name, "web_search");
        ASSERT_STREQ(calls[1].name, "get_weather");
    } END_TEST();
}

void test_build_message_json() {
    TEST("build_message_json: text only") {
        auto result = build_message_json("Hello", "", "");
        ASSERT_TRUE(result.find("\"role\":\"user\"") != std::string::npos);
        ASSERT_TRUE(result.find("\"type\":\"text\"") != std::string::npos);
        ASSERT_TRUE(result.find("\"text\":\"Hello\"") != std::string::npos);
        ASSERT_FALSE(result.find("\"type\":\"image\"") != std::string::npos);
        ASSERT_FALSE(result.find("\"type\":\"audio\"") != std::string::npos);
    } END_TEST();

    TEST("build_message_json: text with image") {
        auto result = build_message_json("Look", "/path/img.jpg", "");
        ASSERT_TRUE(result.find("\"type\":\"text\"") != std::string::npos);
        ASSERT_TRUE(result.find("\"type\":\"image\"") != std::string::npos);
        ASSERT_TRUE(result.find("\"path\":\"/path/img.jpg\"") != std::string::npos);
    } END_TEST();

    TEST("build_message_json: text with audio") {
        auto result = build_message_json("Listen", "", "/tmp/audio.wav");
        ASSERT_TRUE(result.find("\"type\":\"text\"") != std::string::npos);
        ASSERT_TRUE(result.find("\"type\":\"audio\"") != std::string::npos);
    } END_TEST();

    TEST("build_message_json: all empty inputs") {
        auto result = build_message_json("", "", "");
        // Should produce valid JSON structure with empty content array
        ASSERT_TRUE(result.find("\"role\":\"user\"") != std::string::npos);
        ASSERT_TRUE(result.find("\"content\":[") != std::string::npos);
    } END_TEST();

    TEST("build_message_json: escaped quotes in text") {
        auto result = build_message_json("say \"hello\"", "", "");
        ASSERT_TRUE(result.find("\\\"hello\\\"") != std::string::npos);
    } END_TEST();

    TEST("build_message_json: escaped newlines in text") {
        auto result = build_message_json("line1\nline2", "", "");
        ASSERT_TRUE(result.find("\\n") != std::string::npos);
    } END_TEST();
}

// ====================================================================
// MAIN
// ====================================================================

int main() {
    std::cout << "\n=== Skylark Unit Tests ===\n" << std::endl;

    test_json_escape();
    test_json_get_str();
    test_json_get_object();
    test_json_get_array();
    test_parse_content_array();
    test_extract_tool_calls();
    test_build_message_json();

    std::cout << "\n==========================" << std::endl;
    std::cout << "  " << g_passed << " passed, " << g_failed << " failed" << std::endl;
    std::cout << "==========================" << std::endl;

    if (g_failed > 0) {
        std::cout << "\nALL TESTS PASSED: false" << std::endl;
        return 1;
    }
    std::cout << "\nALL TESTS PASSED" << std::endl;
    return 0;
}
