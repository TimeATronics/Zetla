#include "exa_provider.hpp"
#include "../network/http_client.hpp"
#include "../api/json_utils.hpp"
#include "nlohmann/json.hpp"
#include <sstream>

namespace zetla::search {

    static std::string extract_mcp_text(const std::string& json_str);
    static void parse_results_from_text(const std::string& text, int max_results, SearchResponse& resp);

    ExaSearchProvider::ExaSearchProvider(const std::string& api_key, const std::string& mcp_url)
        : api_key_(api_key)
        , mcp_url_(mcp_url.empty() ? "https://mcp.exa.ai/mcp" : mcp_url) {}

    SearchResponse ExaSearchProvider::search(const std::string& query, int num_results) {
        SearchResponse resp;
        resp.success = false;

        try {
            std::string url = mcp_url_;
            if (!api_key_.empty()) {
                std::string sep = (url.find('?') == std::string::npos) ? "?" : "&";
                url += sep + "exaApiKey=" + api_key_;
            }

            int nres = num_results > 0 ? num_results : 5;
            nlohmann::json body = {
                {"jsonrpc", "2.0"},
                {"id", 1},
                {"method", "tools/call"},
                {"params", {
                    {"name", "web_search_exa"},
                    {"arguments", {
                        {"query", query},
                        {"type", "auto"},
                        {"numResults", nres},
                        {"livecrawl", "fallback"},
                        {"contextMaxCharacters", 10000}
                    }}
                }}
            };

            std::string response_body;
            std::string http_error;
            bool http_ok = network::HttpClient::post_stream(url, body.dump(), api_key_,
                [&](const std::string& chunk) { response_body += chunk; },
                http_error, false);

            if (!http_ok) {
                resp.error = http_error.empty() ? "Request failed" : http_error;
                return resp;
            }

            std::string raw = response_body;

            std::string text_content = extract_mcp_text(raw);
            if (text_content.empty()) {
                size_t pos = 0;
                while (pos < raw.size()) {
                    if (raw.substr(pos, 6) == "data: ") {
                        size_t nl = raw.find('\n', pos + 6);
                        if (nl == std::string::npos) nl = raw.size();
                        std::string line = raw.substr(pos + 6, nl - pos - 6);
                        auto trim = line.find_last_not_of(" \t\r");
                        if (trim != std::string::npos) line = line.substr(0, trim + 1);
                        if (!line.empty()) {
                            std::string t = extract_mcp_text(line);
                            if (!t.empty()) {
                                text_content = t;
                                break;
                            }
                        }
                        pos = nl + 1;
                        while (pos < raw.size() && (raw[pos] == '\n' || raw[pos] == '\r')) pos++;
                    } else {
                        pos++;
                    }
                }
            }

            if (text_content.empty()) {
                resp.error = "No text content in Exa response";
                return resp;
            }

            parse_results_from_text(text_content, num_results, resp);

            if (resp.results.empty() && !text_content.empty()) {
                SearchResult sr;
                sr.title = "Exa Search";
                sr.url = "";
                if (text_content.size() > 500) {
                    sr.snippet = text_content.substr(0, 500) + "...";
                } else {
                    sr.snippet = text_content;
                }
                resp.results.push_back(sr);
            }

            resp.success = !resp.results.empty();

        } catch (const std::exception& e) {
            resp.error = e.what();
        }

        return resp;
    }

    static std::string extract_mcp_text(const std::string& json_str) {
        try {
            auto j = nlohmann::json::parse(json_str);
            if (j.contains("result") && j["result"].contains("content") && j["result"]["content"].is_array()) {
                std::string text;
                for (auto& item : j["result"]["content"]) {
                    if (item.contains("type") && item["type"] == "text" && item.contains("text")) {
                        text += item["text"].get<std::string>();
                    }
                }
                return text;
            }
        } catch (...) {}
        return {};
    }

    static void parse_results_from_text(const std::string& text, int max_results, SearchResponse& resp) {
        std::istringstream stream(text);
        std::string line;
        struct { std::string title, url, snippet; } current;
        bool in_result = false;

        while (std::getline(stream, line)) {
            auto ts = line.find_first_not_of(" \t\r\n");
            if (ts == std::string::npos) {
                if (in_result && !current.title.empty()) {
                    resp.results.push_back({current.title, current.url, current.snippet});
                    current = {};
                    in_result = false;
                    if (static_cast<int>(resp.results.size()) >= max_results) break;
                }
                continue;
            }
            line = line.substr(ts);

            bool is_numbered = (line.size() > 2 && std::isdigit(static_cast<unsigned char>(line[0])) &&
                               (line[1] == '.' || line[1] == ')'));

            if (is_numbered) {
                if (in_result && !current.title.empty()) {
                    resp.results.push_back({current.title, current.url, current.snippet});
                    current = {};
                    in_result = false;
                    if (static_cast<int>(resp.results.size()) >= max_results) break;
                }
                size_t ci = line.find_first_not_of(". )0123456789");
                current.title = (ci != std::string::npos) ? line.substr(ci) : line;
                in_result = true;
                continue;
            }

            if (!in_result && !line.empty()) {
                if (line.find("://") == std::string::npos &&
                    line.find("URL:") == std::string::npos &&
                    line.find("Snippet:") == std::string::npos &&
                    line.find("##") == std::string::npos) {
                    current.title = line;
                    in_result = true;
                    continue;
                }
            }

            if (!in_result) continue;

            auto url_prefix = line.find("URL:");
            if (url_prefix != std::string::npos) {
                current.url = line.substr(url_prefix + 4);
                auto tu = current.url.find_first_not_of(" \t");
                if (tu != std::string::npos) current.url = current.url.substr(tu);
                continue;
            }

            auto http_pos = line.find("https://");
            if (http_pos == std::string::npos) http_pos = line.find("http://");
            if (http_pos != std::string::npos && current.url.empty()) {
                auto ue = line.find_first_of(" \t]", http_pos);
                if (ue == std::string::npos) ue = line.size();
                current.url = line.substr(http_pos, ue - http_pos);
            }

            auto snip_prefix = line.find("Snippet:");
            if (snip_prefix != std::string::npos) {
                current.snippet = line.substr(snip_prefix + 8);
                auto ts2 = current.snippet.find_first_not_of(" \t");
                if (ts2 != std::string::npos) current.snippet = current.snippet.substr(ts2);
                continue;
            }

            if (!current.snippet.empty()) {
                current.snippet += " " + line;
            }
        }

        if (in_result && !current.title.empty()) {
            resp.results.push_back({current.title, current.url, current.snippet});
        }
    }

}
