#pragma once
#include "../core/types.hpp"
#include "../route/route.hpp"
#include "../network/http_client.hpp"
#include "../network/sse_parser.hpp"
#include "../protocols/openai_chat/protocol.hpp"
#include "../api/json_utils.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <functional>
#include <memory>
#include <mutex>

namespace zetla::core {

    class RouteProvider : public IProvider {
    public:
        explicit RouteProvider(route::Route chat_route, std::string provider_id)
            : chat_route_(std::move(chat_route))
            , provider_id_(std::move(provider_id)) {}

        void set_route(route::Route r) { chat_route_ = std::move(r); }
        route::Route& route() { return chat_route_; }

        void generate_stream(
            const LLMRequest& request,
            TokenCallback callback
        ) override {
            std::string body = chat_route_.protocol.build_body(request);
            std::string url = chat_route_.render_url(request.model);
            std::string api_key = extract_api_key();

            std::string error;
            route::StreamState state;

            bool ok = network::HttpClient::post_stream(
                url, body, api_key,
                [&](const std::string& chunk) {
                    network::SSEParser parser;
                    parser.set_callback([&](const network::SSEEvent& event) {
                        if (event.is_done) {
                            auto halt_events = protocols::openai_chat::on_halt(state);
                            for (auto& e : halt_events) {
                                StreamChunk sc;
                                if (e.type == LLMEvent::Type::TextDelta) {
                                    sc.delta_content = e.text;
                                } else if (e.type == LLMEvent::Type::ReasoningDelta) {
                                    sc.reasoning = e.text;
                                } else if (e.type == LLMEvent::Type::Finish || e.type == LLMEvent::Type::ProviderError) {
                                    sc.is_finished = true;
                                    if (e.type == LLMEvent::Type::ProviderError) {
                                        sc.delta_content = "Error: " + e.text;
                                    }
                                }
                                if (!sc.delta_content.empty() || !sc.reasoning.empty() || sc.is_finished) {
                                    callback(sc);
                                }
                            }
                            return;
                        }
                        auto events = chat_route_.protocol.parse_step(state, event.data);
                        for (auto& e : events) {
                            StreamChunk sc;
                            if (e.type == LLMEvent::Type::TextDelta) {
                                sc.delta_content = e.text;
                            } else if (e.type == LLMEvent::Type::ReasoningDelta) {
                                sc.reasoning = e.text;
                            } else if (e.type == LLMEvent::Type::Finish || e.type == LLMEvent::Type::ProviderError) {
                                sc.is_finished = true;
                                if (e.type == LLMEvent::Type::ProviderError) {
                                    sc.delta_content = "Error: " + e.text;
                                }
                            }
                            if (!sc.delta_content.empty() || !sc.reasoning.empty() || sc.is_finished) {
                                callback(sc);
                            }
                        }
                    });
                    parser.feed(chunk);
                },
                error
            );

            if (!ok) {
                bool is_abort = error.find("aborted") != std::string::npos ||
                                error.find("cancel") != std::string::npos;
                callback({is_abort ? "" : "Error: " + error, "", true});
            }
        }

        void generate_stream_sse(
            const LLMRequest& request,
            SseCallback callback
        ) override {
            std::string body = chat_route_.protocol.build_body(request);
            std::string url = chat_route_.render_url(request.model);
            std::string api_key = extract_api_key();

            std::string error;
            bool ok = network::HttpClient::post_stream(
                url, body, api_key,
                [&](const std::string& chunk) {
                    network::SSEParser parser;
                    parser.set_callback([&](const network::SSEEvent& event) {
                        if (event.is_done) {
                            callback("[DONE]", true);
                        } else if (!event.data.empty()) {
                            callback(event.data, false);
                        }
                    });
                    parser.feed(chunk);
                },
                error
            );

            if (!ok) {
                nlohmann::json err_j;
                err_j["error"] = error;
                callback(err_j.dump(), true);
            }
        }

        SyncResponse generate_sync(
            const LLMRequest& request
        ) override {
            SyncResponse result;

            LLMRequest req = request;
            if (!req.generation.has_value()) {
                req.generation = GenerationOptions::defaults();
            }

            std::string body_str = chat_route_.protocol.build_body(req);

            auto body_json = nlohmann::json::parse(body_str);
            body_json["stream"] = false;
            auto sync_body = body_json.dump();

            std::string url = chat_route_.render_url(req.model);
            std::string api_key = extract_api_key();
            std::string response;
            std::string error;

            bool ok = network::HttpClient::post_sync(url, sync_body, api_key, response, error);
            if (!ok) {
                result.content = "Error: " + error;
                result.finish_reason = "error";
                return result;
            }

            auto resp_json = nlohmann::json::parse(response, nullptr, false);
            if (resp_json.is_discarded()) {
                result.content = "Error: failed to parse response";
                result.finish_reason = "error";
                return result;
            }

            try {
                if (resp_json.contains("choices") && resp_json["choices"].is_array() && !resp_json["choices"].empty()) {
                    auto& choice = resp_json["choices"][0];

                    if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) {
                        result.finish_reason = choice["finish_reason"].get<std::string>();
                    }

                    if (choice.contains("message")) {
                        auto& msg = choice["message"];

                        if (msg.contains("content") && !msg["content"].is_null()) {
                            result.content = msg["content"].get<std::string>();
                        }

                        if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
                            result.has_tool_calls = true;
                            for (auto& tc : msg["tool_calls"]) {
                                if (!tc.contains("function")) continue;
                                ToolCallRequest tcr;
                                if (tc.contains("id") && tc["id"].is_string()) {
                                    tcr.id = tc["id"].get<std::string>();
                                }
                                auto& func = tc["function"];
                                if (func.contains("name") && func["name"].is_string()) {
                                    tcr.name = func["name"].get<std::string>();
                                }
                                if (func.contains("arguments") && func["arguments"].is_string()) {
                                    tcr.arguments_json = func["arguments"].get<std::string>();
                                }
                                result.tool_calls.push_back(std::move(tcr));
                            }
                        }
                    }
                }
            } catch (const std::exception& e) {
                result.content = "Error: " + std::string(e.what());
                result.finish_reason = "error";
            }

            return result;
        }

        std::string list_models() override {
            return "[]";
        }

        std::string provider_id() const override { return provider_id_; }

    private:
        route::Route chat_route_;
        std::string provider_id_;
        std::string api_key_;

        std::string extract_api_key() const {
            if (chat_route_.auth.has_auth()) {
                route::AuthInput input{"POST", "", ""};
                auto result = chat_route_.auth(input);
                if (!result.has_error && !result.headers.authorization.empty()
                    && result.headers.authorization.size() > 7) {
                    return result.headers.authorization.substr(7);
                }
            }
            return "";
        }
    };
}
