#pragma once
#include <string>
#include <functional>

namespace zetla::route {

    struct EndpointInput {
        std::string model;
    };

    using EndpointPath = std::function<std::string(const EndpointInput&)>;

    struct Endpoint {
        std::string base_url;
        EndpointPath path;
        std::string query;

        std::string render(const EndpointInput& input) const {
            std::string p;
            if (path) {
                p = path(input);
            } else {
                p = "/chat/completions";
            }
            std::string url = base_url;
            if (!url.empty() && url.back() != '/') url += '/';
            if (!p.empty() && p[0] == '/') p = p.substr(1);
            url += p;
            if (!query.empty()) url += "?" + query;
            return url;
        }

        static Endpoint chat(const std::string& base_url) {
            Endpoint e;
            e.base_url = base_url;
            e.path = [](const EndpointInput&) { return std::string("/chat/completions"); };
            return e;
        }

        static Endpoint models(const std::string& base_url) {
            Endpoint e;
            e.base_url = base_url;
            e.path = [](const EndpointInput&) { return std::string("/models"); };
            return e;
        }

        Endpoint with_base_url(const std::string& url) const {
            Endpoint copy = *this;
            copy.base_url = url;
            return copy;
        }

        Endpoint with_path(EndpointPath p) const {
            Endpoint copy = *this;
            copy.path = std::move(p);
            return copy;
        }
    };
}
