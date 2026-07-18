#include "web_search_tool.hpp"
#include "../api/json_utils.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <cctype>

namespace zetla::search {

    static std::string sanitize_utf8(const std::string& input) {
        std::string result;
        result.reserve(input.size());
        for (size_t i = 0; i < input.size();) {
            unsigned char c = static_cast<unsigned char>(input[i]);
            if (c <= 0x7F) {
                result += c;
                i++;
            } else if (c >= 0xC0 && c <= 0xDF && i + 1 < input.size()) {
                result += input.substr(i, 2);
                i += 2;
            } else if (c >= 0xE0 && c <= 0xEF && i + 2 < input.size()) {
                result += input.substr(i, 3);
                i += 3;
            } else if (c >= 0xF0 && c <= 0xF7 && i + 3 < input.size()) {
                result += input.substr(i, 4);
                i += 4;
            } else {
                result += '?';
                i++;
            }
        }
        return result;
    }

    WebSearchTool::WebSearchTool(std::unique_ptr<ISearchProvider> provider, int num_results)
        : search_provider_(std::move(provider))
        , num_results_(num_results) {}

    std::string WebSearchTool::name() const {
        return "web_search";
    }

    std::string WebSearchTool::description() const {
        return "Search the web or fetch a URL. Use mode 'search' with a query to find information, "
               "or mode 'fetch' with a URL to retrieve the full content of a web page.";
    }

    std::string WebSearchTool::parameters_schema() const {
        return R"json({
            "type": "object",
            "properties": {
                "mode": {
                    "type": "string",
                    "enum": ["search", "fetch"],
                    "description": "'search' to find web results for a query, 'fetch' to get the full content of a URL"
                },
                "query": {
                    "type": "string",
                    "description": "The search query (required when mode is 'search')"
                },
                "url": {
                    "type": "string",
                    "description": "The URL to fetch (required when mode is 'fetch')"
                }
            },
            "required": ["mode"]
        })json";
    }

    core::ToolCallResult WebSearchTool::execute(const core::ToolCallRequest& call) {
        if (call.arguments_json.empty()) {
            core::ToolCallResult r;
            r.tool_call_id = call.id;
            r.is_error = true;
            nlohmann::json j;
            j["error"] = "No arguments provided";
            r.content = j.dump();
            return r;
        }

        std::string mode = zetla::json::extract_string(call.arguments_json, "mode");

        if (mode == "fetch") {
            std::string url = zetla::json::extract_string(call.arguments_json, "url");
            if (url.empty()) {
                core::ToolCallResult r;
                r.tool_call_id = call.id;
                r.is_error = true;
                nlohmann::json j;
                j["error"] = "No URL provided";
                r.content = j.dump();
                return r;
            }
            return do_fetch(url, call.id);
        }

        std::string query = zetla::json::extract_string(call.arguments_json, "query");
        if (query.empty()) {
            core::ToolCallResult r;
            r.tool_call_id = call.id;
            r.is_error = true;
            nlohmann::json j;
            j["error"] = "No search query provided";
            r.content = j.dump();
            return r;
        }
        return do_search(query, call.id);
    }

    core::ToolCallResult WebSearchTool::do_search(const std::string& query, const std::string& tool_call_id) {
        core::ToolCallResult result;
        result.tool_call_id = tool_call_id;
        result.is_error = false;

        SearchResponse resp = search_provider_->search(query, num_results_);

        if (!resp.success) {
            nlohmann::json j;
            j["error"] = "Search failed: " + resp.error;
            result.content = j.dump();
            result.is_error = true;
            return result;
        }

        nlohmann::json out;
        out["mode"] = "search";
        out["query"] = query;
        out["results"] = nlohmann::json::array();
        for (auto& r : resp.results) {
            out["results"].push_back({{"title", r.title}, {"url", r.url}, {"snippet", r.snippet}});
        }
        out["provider"] = search_provider_->name();
        result.content = out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        return result;
    }

    core::ToolCallResult WebSearchTool::do_fetch(const std::string& url, const std::string& tool_call_id) {
        core::ToolCallResult result;
        result.tool_call_id = tool_call_id;
        result.is_error = false;

        std::string raw;
        std::string error;
        bool ok = network::HttpClient::get_sync(url, "", raw, error);

        if (!ok) {
            nlohmann::json j;
            j["error"] = "Failed to fetch URL: " + error;
            result.content = j.dump();
            result.is_error = true;
            return result;
        }

        std::string cleaned = sanitize_utf8(raw);

        nlohmann::json out;
        out["mode"] = "fetch";
        out["url"] = url;
        out["content"] = cleaned;
        out["content_length"] = static_cast<int>(cleaned.size());
        result.content = out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        return result;
    }

}
