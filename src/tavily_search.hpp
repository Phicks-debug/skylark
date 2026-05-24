// Tavily web search tool - integrates with Tavily Search API
// Mirrors the Python TavilySearchTool

#ifndef TAVILY_SEARCH_HPP
#define TAVILY_SEARCH_HPP

#include <string>
#include <string_view>
#include <vector>

namespace tavily_search {

struct SearchResult {
    std::string title;
    std::string url;
    std::string content;
    double score = 0.0;
};

// Perform a web search using the Tavily API
// Returns search results, or empty vector on failure
std::vector<SearchResult> search(std::string_view query,
                                 std::string_view api_key,
                                 int max_results = 5,
                                 std::string_view search_depth = "basic");

// Format search results into a JSON string suitable for the LLM
std::string format_results_json(std::string_view query,
                                const std::vector<SearchResult>& results);

// Generate the tools JSON for conversation config
// Returns a JSON array string describing the search tool
std::string get_tool_definition();

} // namespace tavily_search

#endif // TAVILY_SEARCH_HPP
