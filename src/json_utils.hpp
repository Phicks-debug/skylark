// Shared JSON utility functions used across the project.
// Header-only to avoid needing a separate compilation unit.

#ifndef JSON_UTILS_HPP
#define JSON_UTILS_HPP

#include <string>
#include <string_view>

namespace json_utils {

// ---- Escape string for JSON ----
inline std::string json_escape(std::string_view str) {
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

// ---- Unescape JSON string escapes (reverse of json_escape) ----
inline std::string json_unescape(std::string_view str) {
    std::string out;
    out.reserve(str.length());
    for (size_t i = 0; i < str.length(); i++) {
        if (str[i] == '\\' && i + 1 < str.length()) {
            switch (str[i + 1]) {
            case 'n':  out += '\n'; i++; break;
            case 't':  out += '\t'; i++; break;
            case 'r':  out += '\r'; i++; break;
            case '"':  out += '"'; i++; break;
            case '\\': out += '\\'; i++; break;
            case 'b':  out += '\b'; i++; break;
            case 'f':  out += '\f'; i++; break;
            default:   out += str[i]; break;
            }
        } else {
            out += str[i];
        }
    }
    return out;
}

// ---- Extract a JSON-quoted string (handles \" and \\ escapes) ----
// `start` must point to the opening ". Returns raw content (with escapes preserved)
// and sets `end_pos` to position after the closing ".
inline std::string json_extract_raw_string(std::string_view json, size_t start, size_t& end_pos) {
    if (start >= json.length() || json[start] != '"') {
        end_pos = start;
        return "";
    }
    std::string raw;
    size_t i = start + 1;
    while (i < json.length()) {
        if (json[i] == '\\' && i + 1 < json.length()) {
            raw += json[i];
            raw += json[i + 1];
            i += 2;
        } else if (json[i] == '"') {
            end_pos = i + 1;
            return raw;
        } else {
            raw += json[i];
            i++;
        }
    }
    end_pos = i;
    return raw;
}

// ---- Minimal JSON string extractor ----
inline std::string json_get_str(std::string_view json, std::string_view key) {
    std::string search = std::string("\"") + std::string(key) + "\"";
    auto pos = json.find(search);
    if (pos == std::string_view::npos) return "";
    auto colon = json.find(':', pos + search.length());
    if (colon == std::string_view::npos) return "";
    auto open = json.find('"', colon + 1);
    if (open == std::string_view::npos) return "";
    
    // Find the closing quote, handling escaped quotes
    size_t close = open + 1;
    while (close < json.length()) {
        if (json[close] == '\\') {
            close += 2; // skip escaped character
        } else if (json[close] == '"') {
            break;
        } else {
            close++;
        }
    }
    if (close >= json.length()) return "";
    return json_unescape(json.substr(open + 1, close - open - 1));
}

// ---- Extract raw JSON object after a key (for nested objects) ----
inline std::string json_get_object(std::string_view json, std::string_view key) {
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
inline std::string json_get_array(std::string_view json, std::string_view key) {
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

} // namespace json_utils

#endif // JSON_UTILS_HPP
