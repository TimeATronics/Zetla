#pragma once
#include "protocol.hpp"
#include "endpoint.hpp"
#include "auth.hpp"
#include "../core/types.hpp"
#include "../core/error.hpp"
#include <string>
#include <functional>
#include <optional>
#include <sstream>
#include <vector>

namespace zetla::route {

    struct RouteDefaults {
        std::optional<core::GenerationOptions> generation;
        core::ProviderOptions provider_options;
        std::optional<std::string> model_prefix_to_strip;
        std::optional<std::string> reasoning_key;
        bool supports_frequency_penalty = true;
        bool supports_presence_penalty = true;
        bool supports_response_format = true;
        bool supports_reasoning_effort = false;
        std::vector<std::string> extra_headers;
    };

    struct Route {
        std::string id;
        Protocol protocol;
        Endpoint endpoint;
        Auth auth;
        RouteDefaults defaults;

        Route() = default;
        Route(std::string id_, Protocol protocol_, Endpoint endpoint_, Auth auth_, RouteDefaults defaults_)
            : id(std::move(id_)), protocol(std::move(protocol_)), endpoint(std::move(endpoint_)), auth(std::move(auth_)), defaults(std::move(defaults_)) {}

        Route with_auth(Auth new_auth) const {
            Route copy = *this;
            copy.auth = std::move(new_auth);
            return copy;
        }

        Route with_endpoint(Endpoint new_endpoint) const {
            Route copy = *this;
            copy.endpoint = std::move(new_endpoint);
            return copy;
        }

        Route with_defaults(RouteDefaults d) const {
            Route copy = *this;
            copy.defaults = std::move(d);
            return copy;
        }

        std::string render_url(const std::string& model_id) const {
            return endpoint.render(EndpointInput{model_id});
        }

        std::string strip_model_prefix(const std::string& model) const {
            if (defaults.model_prefix_to_strip.has_value()) {
                const auto& prefix = defaults.model_prefix_to_strip.value();
                if (model.size() > prefix.size() && model.compare(0, prefix.size(), prefix) == 0) {
                    return model.substr(prefix.size());
                }
            }
            return model;
        }

        std::string build_body(const core::LLMRequest& req) const {
            if (!protocol.build_body) return "{}";
            return protocol.build_body(req, defaults);
        }
    };
}
