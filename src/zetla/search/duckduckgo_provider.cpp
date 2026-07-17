#include "duckduckgo_provider.hpp"
#include "../network/http_client.hpp"
#include <regex>
#include <sstream>
#include <iomanip>

namespace zetla::search {

    static std::string url_encode(const std::string& value) {
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;
        for (auto c : value) {
            if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
                escaped << c;
            } else {
                escaped << '%' << std::setw(2) << std::setfill('0') << ((int)static_cast<unsigned char>(c));
            }
        }
        return escaped.str();
    }

    static std::string url_decode(const std::string& encoded) {
        std::string result;
        result.reserve(encoded.size());
        for (size_t i = 0; i < encoded.size(); ++i) {
            if (encoded[i] == '%' && i + 2 < encoded.size()) {
                int val;
                std::istringstream iss(encoded.substr(i + 1, 2));
                if (iss >> std::hex >> val) {
                    result += static_cast<char>(val);
                    i += 2;
                } else {
                    result += encoded[i];
                }
            } else if (encoded[i] == '+') {
                result += ' ';
            } else {
                result += encoded[i];
            }
        }
        return result;
    }

    static std::string strip_html(const std::string& html) {
        std::regex tag_regex("<[^>]*>");
        std::string result = std::regex_replace(html, tag_regex, "");
        auto decode_entity = [&](const std::string& entity, char ch) {
            size_t pos;
            while ((pos = result.find(entity)) != std::string::npos) {
                result.replace(pos, entity.length(), 1, ch);
            }
        };
        decode_entity("&amp;", '&');
        decode_entity("&lt;", '<');
        decode_entity("&gt;", '>');
        decode_entity("&quot;", '"');
        decode_entity("&#39;", '\'');
        decode_entity("&nbsp;", ' ');
        return result;
    }

    SearchResponse DuckDuckGoSearchProvider::search(const std::string& query, int num_results) {
        SearchResponse resp;
        resp.success = false;

        try {
            std::string url = "https://html.duckduckgo.com/html/?q=" + url_encode(query);

            auto http_resp = network::HttpClient::get(url, "");

            if (!http_resp.success() || http_resp.body.empty()) {
                resp.error = http_resp.error.empty() ? "Request failed" : http_resp.error;
                return resp;
            }

            std::string body = http_resp.body;

            // Parse results using simple string searching
            // Find result blocks: <div class="result"> ... </div>
            std::string result_marker = "result__a";
            std::string snippet_marker = "result__snippet";
            size_t pos = 0;

            while (static_cast<int>(resp.results.size()) < num_results) {
                // Find next result link
                size_t link_start = body.find(result_marker, pos);
                if (link_start == std::string::npos) break;

                // Find href
                size_t href_start = body.find("href=\"", link_start);
                if (href_start == std::string::npos) break;
                href_start += 6; // skip past href="
                size_t href_end = body.find("\"", href_start);
                if (href_end == std::string::npos) break;

                std::string raw_url = body.substr(href_start, href_end - href_start);

                // Find title text (between > and </a>)
                size_t title_start = body.find(">", href_end);
                if (title_start == std::string::npos) break;
                title_start += 1;
                size_t title_end = body.find("</a>", title_start);
                if (title_end == std::string::npos) break;

                std::string title = strip_html(body.substr(title_start, title_end - title_start));

                // Find snippet
                size_t snippet_pos = body.find(snippet_marker, title_end);
                if (snippet_pos == std::string::npos) break;

                size_t snip_href_start = body.find(">", snippet_pos);
                if (snip_href_start == std::string::npos) break;
                snip_href_start += 1;
                size_t snip_href_end = body.find("</a>", snip_href_start);
                if (snip_href_end == std::string::npos) break;

                std::string snippet = strip_html(body.substr(snip_href_start, snip_href_end - snip_href_start));

                // Extract actual URL from DDG redirect
                std::string actual_url = raw_url;
                size_t uddg_pos = raw_url.find("uddg=");
                if (uddg_pos != std::string::npos) {
                    size_t val_start = uddg_pos + 6;
                    size_t val_end = raw_url.find("&", val_start);
                    if (val_end == std::string::npos) val_end = raw_url.size();
                    actual_url = url_decode(raw_url.substr(val_start, val_end - val_start));
                }

                if (!title.empty()) {
                    SearchResult result;
                    result.title = title;
                    result.url = actual_url;
                    result.snippet = snippet;
                    resp.results.push_back(result);
                }

                pos = snip_href_end + 4;
            }

            resp.success = true;
        } catch (const std::exception& e) {
            resp.error = e.what();
        }

        return resp;
    }

}
