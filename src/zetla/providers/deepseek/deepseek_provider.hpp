#pragma once
#include "../provider_facade.hpp"
#include "../../protocols/openai_chat/protocol.hpp"
#include "../../api/json_utils.hpp"
#include "../../network/http_client.hpp"
#include "nlohmann/json.hpp"

namespace zetla::providers {

    class DeepseekProvider : public IProviderFacade, public RouteProviderMixin<DeepseekProvider> {
    public:
        static DeepseekProvider configure(const std::string& api_key, const std::string& base_url = "") {
            DeepseekProvider p;
            p.api_key_ = api_key;
            std::string url = base_url.empty() ? "https://api.deepseek.com" : base_url;

            route::RouteDefaults d;
            d.supports_frequency_penalty = false;
            d.supports_presence_penalty = false;
            d.supports_response_format = false;
            d.supports_reasoning_effort = true;
            d.reasoning_key = "thinking";

            p.chat_route_ = route::Route{
                "openai-chat",
                protocols::openai_chat::make_protocol(),
                route::Endpoint::chat(url),
                route::Auth::bearer(api_key),
                d
            };

            p.models_route_ = route::Route{
                "openai-models",
                {},
                route::Endpoint::models(url),
                route::Auth::bearer(api_key),
                {}
            };

            return p;
        }

        std::string provider_id() const override { return "deepseek"; }
        std::string provider_name() const override { return "Deepseek"; }
        route::Route& chat_route() override { return chat_route_; }
        route::Route& models_route() override { return models_route_; }

        void generate_stream(const core::LLMRequest& request, core::TokenCallback callback) override {
            route_generate_stream(chat_route_, request, callback);
        }
        void generate_stream_sse(const core::LLMRequest& request, core::SseCallback callback) override {
            route_generate_stream_sse(chat_route_, request, callback);
        }
        core::SyncResponse generate_sync(const core::LLMRequest& request) override {
            return route_generate_sync(chat_route_, request);
        }

        std::string list_models() override {
            std::string url = models_route_.render_url("");
            std::string raw;
            std::string error;
            bool ok = network::HttpClient::get_sync(url, api_key_, raw, error, false, true);
            if (!ok) return "[]";

            auto j = zetla::json::try_parse(raw);
            if (!j || !(*j).contains("data") || !(*j)["data"].is_array()) return "[]";

            nlohmann::json result = nlohmann::json::array();
            for (auto& model : (*j)["data"]) {
                std::string id = model.value("id", "");
                if (id.empty()) continue;

                nlohmann::json entry;
                entry["id"] = id;
                entry["name"] = id;
                entry["provider"] = "deepseek";

                nlohmann::json caps;
                caps["supports_vision"] = false;
                caps["supports_tools"] = true;
                caps["supports_reasoning"] = true;
                caps["context_window"] = 65536;
                caps["max_output_tokens"] = 8192;
                caps["thinking_levels"] = {"disabled", "low", "medium", "high"};
                caps["supported_params"] = {"temperature", "max_tokens", "top_p", "reasoning_effort"};
                entry["capabilities"] = caps;
                result.push_back(entry);
            }

            return result.dump();
        }

    private:
        std::string api_key_;
        route::Route chat_route_;
        route::Route models_route_;
    };
}
