// Tavily web search implementation using libcurl

#include "tavily_search.hpp"
#include "terminal.hpp"
#include <curl/curl.h>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace tavily_search {

namespace {

// Callback for capturing HTTP response body
size_t write_callback(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* response = static_cast<std::string*>(userdata);
    size_t total = size * nmemb;
    response->append(static_cast<const char*>(ptr), total);
    return total;
}

// Minimal JSON value parser - just enough for field extraction
class SimpleJson {
public:
    static std::string get_string(std::string_view json, std::string_view key) {
        std::string search = std::string("\"") + std::string(key) + "\"";
        auto pos = json.find(search);
        if (pos == std::string_view::npos) return "";

        // Find the colon after the key
        auto colon = json.find(':', pos + search.length());
        if (colon == std::string_view::npos) return "";

        // Find opening quote of value
        auto open = json.find('"', colon + 1);
        if (open == std::string_view::npos) return "";

        // Find closing quote
        auto close = json.find('"', open + 1);
        if (close == std::string_view::npos) return "";

        return std::string(json.substr(open + 1, close - open - 1));
    }

    static double get_double(std::string_view json, std::string_view key) {
        std::string search = std::string("\"") + std::string(key) + "\"";
        auto pos = json.find(search);
        if (pos == std::string_view::npos) return 0.0;

        auto colon = json.find(':', pos + search.length());
        if (colon == std::string_view::npos) return 0.0;

        // Skip whitespace after colon
        auto start = colon + 1;
        while (start < json.length() &&
               (json[start] == ' ' || json[start] == '\t' || json[start] == '\n')) {
            start++;
        }

        // Read until non-numeric
        std::string num;
        while (start < json.length() &&
               (json[start] == '-' || json[start] == '.' ||
                (json[start] >= '0' && json[start] <= '9'))) {
            num += json[start];
            start++;
        }

        try {
            return std::stod(num);
        } catch (...) {
            return 0.0;
        }
    }

    // Find all occurrences of a key's array/object value between { ... }
    static std::vector<std::string> extract_objects(std::string_view json, std::string_view key) {
        std::vector<std::string> results;
        std::string search = std::string("\"") + std::string(key) + "\"";
        auto pos = json.find(search);
        if (pos == std::string_view::npos) return results;

        // Find the colon
        auto colon = json.find(':', pos + search.length());
        if (colon == std::string_view::npos) return results;

        // Find opening bracket
        auto open_bracket = json.find('[', colon + 1);
        if (open_bracket == std::string_view::npos) return results;

        // Now extract individual objects from the array
        size_t current = open_bracket + 1;
        while (current < json.length()) {
            // Find the next '{'
            auto obj_start = json.find('{', current);
            if (obj_start == std::string_view::npos || obj_start >= json.find(']', open_bracket)) {
                break;
            }

            // Find the matching '}'
            int depth = 0;
            for (size_t i = obj_start; i < json.length(); i++) {
                if (json[i] == '{') depth++;
                else if (json[i] == '}') {
                    depth--;
                    if (depth == 0) {
                        results.push_back(std::string(json.substr(obj_start, i - obj_start + 1)));
                        current = i + 1;
                        break;
                    }
                }
                // Skip strings
                if (json[i] == '"') {
                    i++;
                    while (i < json.length() && json[i] != '"') {
                        if (json[i] == '\\') i++; // skip escaped
                        i++;
                    }
                }
            }
            if (depth != 0) break; // Unbalanced
        }

        return results;
    }

    // Escape a string for JSON
    static std::string escape(std::string_view str) {
        std::string result;
        for (char c : str) {
            switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c;
            }
        }
        return result;
    }
};

} // namespace

std::vector<SearchResult> search(std::string_view query,
                                 std::string_view api_key,
                                 int max_results,
                                 std::string_view search_depth) {
    std::vector<SearchResult> results;
    CURL* curl = curl_easy_init();
    if (!curl) {
        terminal::cprintln("Failed to initialize curl for Tavily search",
                          terminal::Color::Red);
        return results;
    }

    // Build JSON request body
    std::ostringstream body;
    body << "{"
         << "\"api_key\":\"" << SimpleJson::escape(api_key) << "\","
         << "\"query\":\"" << SimpleJson::escape(query) << "\","
         << "\"search_depth\":\"" << SimpleJson::escape(search_depth) << "\","
         << "\"max_results\":" << max_results
         << "}";

    std::string body_str = body.str();
    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.tavily.com/search");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(body_str.length()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "tiny-habibi/1.0");

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        terminal::cprintln(std::string("Tavily search failed: ") +
                          curl_easy_strerror(res),
                          terminal::Color::Red);
        return results;
    }

    // Parse the JSON response
    auto result_objects = SimpleJson::extract_objects(response, "results");
    for (const auto& obj : result_objects) {
        SearchResult sr;
        sr.title = SimpleJson::get_string(obj, "title");
        sr.url = SimpleJson::get_string(obj, "url");
        sr.content = SimpleJson::get_string(obj, "content");
        sr.score = SimpleJson::get_double(obj, "score");
        results.push_back(std::move(sr));
    }

    return results;
}

std::string format_results_json(std::string_view query,
                                const std::vector<SearchResult>& results) {
    std::ostringstream json;
    json << "{\"query\":\"" << SimpleJson::escape(query) << "\","
         << "\"results\":[";

    for (size_t i = 0; i < results.size(); i++) {
        if (i > 0) json << ",";
        json << "{"
             << "\"title\":\"" << SimpleJson::escape(results[i].title) << "\","
             << "\"url\":\"" << SimpleJson::escape(results[i].url) << "\","
             << "\"content\":\"" << SimpleJson::escape(results[i].content) << "\","
             << "\"score\":" << results[i].score
             << "}";
    }

    json << "]}";
    return json.str();
}

std::string get_tool_definition() {
    return R"([
  {
    "type": "function",
    "function": {
      "name": "web_search",
      "description": "Search the web for information on a given query. Returns a list of search results with titles, URLs, and content snippets.",
      "parameters": {
        "type": "object",
        "properties": {
          "query": {
            "type": "string",
            "description": "The search query to look up"
          }
        },
        "required": ["query"]
      }
    }
  }
])";
}

} // namespace tavily_search
