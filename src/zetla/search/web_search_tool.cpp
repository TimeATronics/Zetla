#include "web_search_tool.hpp"
#include "../api/json_utils.hpp"
#include "nlohmann/json.hpp"

namespace zetla::search {

    WebSearchTool::WebSearchTool(std::unique_ptr<ISearchProvider> provider, int num_results)
        : search_provider_(std::move(provider))
        , num_results_(num_results) {}

    std::string WebSearchTool::name() const {
        return "web_search";
    }

    std::string WebSearchTool::description() const {
        return "Search the web for up-to-date information. Returns search results with titles, URLs, and snippets. Cannot fetch full page content from linked URLs.";
    }

    std::string WebSearchTool::parameters_schema() const {
        return R"({
            "type": "object",
            "properties": {
                "query": {
                    "type": "string",
                    "description": "The search query to find information about"
                }
            },
            "required": ["query"]
        })";
    }

    core::ToolCallResult WebSearchTool::execute(const core::ToolCallRequest& call) {
        core::ToolCallResult result;
        result.tool_call_id = call.id;
        result.is_error = false;

        try {
            std::string query = zetla::json::extract_string(call.arguments_json, "query");

            if (query.empty()) {
                nlohmann::json err_j;
                err_j["error"] = "No search query provided";
                result.content = err_j.dump();
                result.is_error = true;
                return result;
            }

            SearchResponse resp = search_provider_->search(query, num_results_);

            if (!resp.success) {
                nlohmann::json err_j;
                err_j["error"] = "Search failed: " + resp.error;
                result.content = err_j.dump();
                result.is_error = true;
                return result;
            }

            nlohmann::json out;
            out["query"] = query;
            out["results"] = nlohmann::json::array();
            for (auto& r : resp.results) {
                out["results"].push_back({{"title", r.title}, {"url", r.url}, {"snippet", r.snippet}});
            }
            out["provider"] = search_provider_->name();
            result.content = out.dump();
        } catch (const std::exception& e) {
            nlohmann::json err_j;
            err_j["error"] = std::string(e.what());
            result.content = err_j.dump();
            result.is_error = true;
        }

        return result;
    }

}
