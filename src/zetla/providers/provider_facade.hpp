#pragma once
#include "../core/types.hpp"
#include "../core/log.hpp"
#include "../route/route.hpp"
#include "../route/auth.hpp"
#include "../network/http_client.hpp"
#include "../network/sse_parser.hpp"
#include "../protocols/openai_chat/protocol.hpp"
#include "../api/json_utils.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <memory>

namespace zetla::providers {

    struct ModelCapabilities {
        bool supports_vision = false;
        bool supports_tools = true;
        bool supports_reasoning = false;
        int context_window = 8192;
        int max_output_tokens = 4096;
        std::vector<std::string> supported_params;
        std::vector<std::string> thinking_levels;
    };

    struct ModelInfo {
        std::string id;
        std::string name;
        std::string provider;
        ModelCapabilities capabilities;
    };

    class IProviderFacade : public core::IProvider {
    public:
        virtual ~IProviderFacade() = default;
        virtual std::string provider_name() const = 0;
        virtual route::Route& chat_route() = 0;
        virtual route::Route& models_route() = 0;
    };

    template<typename Derived>
    class RouteProviderMixin {
    protected:
        void route_generate_stream(
            route::Route& chat_route,
            const core::LLMRequest& request,
            core::TokenCallback callback
        ) {
            std::string body = chat_route.protocol.build_body(request);
            // Add streaming fields to body
            auto body_json = nlohmann::json::parse(body);
            body_json["stream"] = true;
            body_json["stream_options"] = {{"include_usage", true}};
            body = body_json.dump();

            std::string url = chat_route.render_url(request.model);
            std::string api_key = extract_route_api_key(chat_route);

            ZLOGI("route_generate_stream: model=%s url=%s", request.model.c_str(), url.c_str());
            ZLOGI("route_generate_stream: body=%s", log::truncate(body, 1500).c_str());
            ZLOGI("route_generate_stream: api_key=%s", log::mask_key(api_key).c_str());
            ZLOGI("route_generate_stream: system_prompt=%s", log::truncate(request.system_prompt, 200).c_str());
            ZLOGI("route_generate_stream: messages_count=%zu tools_count=%zu", request.messages.size(), request.tools.size());
            if (request.generation.has_value()) {
                auto& g = request.generation.value();
                ZLOGI("route_generate_stream: temp=%s max_tokens=%s",
                    g.temperature.has_value() ? std::to_string(g.temperature.value()).c_str() : "none",
                    g.max_tokens.has_value() ? std::to_string(g.max_tokens.value()).c_str() : "none");
            }

            std::string error;
            route::StreamState state;
            network::SSEParser parser;

            parser.set_callback([&](const network::SSEEvent& event) {
                if (event.is_done) {
                    auto halt_events = protocols::openai_chat::on_halt(state);
                    for (auto& e : halt_events) {
                        core::StreamChunk sc;
                        if (e.type == core::LLMEvent::Type::TextDelta) {
                            sc.delta_content = e.text;
                        } else if (e.type == core::LLMEvent::Type::ReasoningDelta) {
                            sc.reasoning = e.text;
                        } else if (e.type == core::LLMEvent::Type::Finish || e.type == core::LLMEvent::Type::ProviderError) {
                            sc.is_finished = true;
                            if (e.type == core::LLMEvent::Type::ProviderError) {
                                sc.delta_content = "Error: " + e.text;
                            }
                        }
                        if (!sc.delta_content.empty() || !sc.reasoning.empty() || sc.is_finished) {
                            callback(sc);
                        }
                    }
                    return;
                }
                auto events = chat_route.protocol.parse_step(state, event.data);
                for (auto& e : events) {
                    core::StreamChunk sc;
                    if (e.type == core::LLMEvent::Type::TextDelta) {
                        sc.delta_content = e.text;
                    } else if (e.type == core::LLMEvent::Type::ReasoningDelta) {
                        sc.reasoning = e.text;
                    } else if (e.type == core::LLMEvent::Type::Finish || e.type == core::LLMEvent::Type::ProviderError) {
                        sc.is_finished = true;
                        if (e.type == core::LLMEvent::Type::ProviderError) {
                            sc.delta_content = "Error: " + e.text;
                        }
                    }
                    if (!sc.delta_content.empty() || !sc.reasoning.empty() || sc.is_finished) {
                        callback(sc);
                    }
                }
            });

            bool ok = network::HttpClient::post_stream(
                url, body, api_key,
                [&](const std::string& chunk) {
                    parser.feed(chunk);
                },
                error
            );

            if (!ok && !error.empty()) {
                callback({"Error: " + error, "", true});
            } else if (!ok) {
                callback({"Request stopped.", "", true});
            }
        }

        void route_generate_stream_sse(
            route::Route& chat_route,
            const core::LLMRequest& request,
            core::SseCallback callback
        ) {
            std::string body = chat_route.protocol.build_body(request);
            std::string url = chat_route.render_url(request.model);
            std::string api_key = extract_route_api_key(chat_route);

            ZLOGI("route_generate_stream_sse: model=%s url=%s", request.model.c_str(), url.c_str());
            ZLOGI("route_generate_stream_sse: body=%s", log::truncate(body, 1500).c_str());

            std::string error;
            network::SSEParser parser;
            parser.set_callback([&](const network::SSEEvent& event) {
                if (event.is_done) {
                    callback("[DONE]", true);
                } else if (!event.data.empty()) {
                    callback(event.data, false);
                }
            });

            bool ok = network::HttpClient::post_stream(
                url, body, api_key,
                [&](const std::string& chunk) {
                    parser.feed(chunk);
                },
                error
            );

            if (!ok && !error.empty()) {
                nlohmann::json err_j;
                err_j["error"] = error;
                callback(err_j.dump(), true);
            } else if (!ok) {
                callback("{\"error\":\"Request stopped.\"}", true);
            }
        }

        core::SyncResponse route_generate_sync(
            route::Route& chat_route,
            const core::LLMRequest& request
        ) {
            core::SyncResponse result;

            core::LLMRequest req = request;
            if (!req.generation.has_value()) {
                req.generation = core::GenerationOptions::defaults();
            }

            std::string body_str = chat_route.protocol.build_body(req);

            auto body_json = nlohmann::json::parse(body_str);
            body_json["stream"] = false;
            auto sync_body = body_json.dump();

            std::string url = chat_route.render_url(req.model);
            std::string api_key = extract_route_api_key(chat_route);
            std::string response;
            std::string error;

            bool ok = network::HttpClient::post_sync(url, sync_body, api_key, response, error);
            if (!ok) {
                result.content = "Error: " + error;
                result.finish_reason = "error";
                return result;
            }

            auto j = zetla::json::try_parse(response);
            if (!j) {
                result.content = "Error: failed to parse response";
                result.finish_reason = "error";
                return result;
            }

            try {
                auto& choices = (*j)["choices"];
                if (choices.is_array() && !choices.empty()) {
                    auto& choice = choices[0];

                    if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) {
                        result.finish_reason = choice["finish_reason"].get<std::string>();
                    }

                    if (choice.contains("message")) {
                        auto& msg = choice["message"];

                        if (msg.contains("content") && !msg["content"].is_null()) {
                            result.content = msg["content"].get<std::string>();
                        }

                        if (msg.contains("reasoning_content") && msg["reasoning_content"].is_string() && !msg["reasoning_content"].get<std::string>().empty()) {
                            result.reasoning = msg["reasoning_content"].get<std::string>();
                        }

                        if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
                            result.has_tool_calls = true;
                            for (auto& tc : msg["tool_calls"]) {
                                if (!tc.contains("function")) continue;
                                core::ToolCallRequest tcr;
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
                                if (!tcr.name.empty()) {
                                    result.tool_calls.push_back(std::move(tcr));
                                }
                            }
                        }

                        // DSML format tool calls (DeepSeek native format)
                        // Tag format: <U+FF5C|DSML|U+FF5C|invoke name="...">...
                        if (!result.has_tool_calls && !result.content.empty()) {
                            auto& c = result.content;
                            // UTF-8 encoding of U+FF5C (fullwidth vertical line) is 0xEF 0xBD 0x9C
                            const std::string ff5c = "\xEF\xBD\x9C";
                            const std::string dsml_invoke = "<" + ff5c + "DSML" + ff5c + "invoke";
                            auto dsml_pos = c.find(dsml_invoke);
                            if (dsml_pos != std::string::npos) {
                                result.has_tool_calls = true;
                                static std::atomic<int> dsml_id{0};
                                const std::string dsml_tag_prefix = "<" + ff5c + "DSML" + ff5c;
                                size_t pos = 0;
                                while (true) {
                                    auto invoke_at = c.find(dsml_invoke, pos);
                                    if (invoke_at == std::string::npos) break;
                                    // extract name="..."
                                    auto na = c.find("name=\"", invoke_at);
                                    if (na == std::string::npos) { pos = invoke_at + 1; continue; }
                                    na += 6;
                                    auto ne = c.find('"', na);
                                    if (ne == std::string::npos) break;
                                    std::string tname = c.substr(na, ne - na);
                                    if (tname.empty()) { pos = invoke_at + 1; continue; }
                                    core::ToolCallRequest tcr;
                                    tcr.id = "dsml_call_" + std::to_string(++dsml_id);
                                    tcr.name = tname;
                                    nlohmann::json jargs = nlohmann::json::object();
                                    // collect parameters until next invoke or end
                                    size_t scan = invoke_at + dsml_invoke.size();
                                    while (scan < c.size()) {
                                        auto ni = c.find(ff5c + "DSML" + ff5c, scan);
                                        if (ni == std::string::npos) break;
                                        // check if it's a parameter tag (preceded by <)
                                        if (ni < 1 || c[ni-1] != '<') { scan = ni + 1; continue; }
                                        // check if it's an invoke (next token)
                                        std::string after = c.substr(ni + ff5c.size() + 4 + ff5c.size(), 7); // "DSML" + ff5c = 5+3 chars
                                        if (after.find("invoke") == 0) break; // next invoke, done with params
                                        if (after.find("param") != 0 && after.find("Param") != 0) { scan = ni + 1; continue; }
                                        // it's a parameter tag: <|DSML|parameter name="x" string="true">value
                                        auto pna = c.find("name=\"", ni);
                                        if (pna == std::string::npos) { scan = ni + 1; continue; }
                                        pna += 6;
                                        auto pne = c.find('"', pna);
                                        if (pne == std::string::npos) break;
                                        std::string pname = c.substr(pna, pne - pna);
                                        bool is_str = c.find("string=\"true\"", ni) != std::string::npos &&
                                                      c.find("string=\"true\"", ni) < c.find('>', ni);
                                        auto vs = c.find('>', pne);
                                        if (vs == std::string::npos) break;
                                        vs++;
                                        auto ve = c.find("<" + ff5c + "DSML", vs);
                                        if (ve == std::string::npos) ve = c.size();
                                        std::string v = c.substr(vs, ve - vs);
                                        while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
                                        if (is_str) jargs[pname] = v;
                                        else { try { jargs[pname] = std::stod(v); } catch (...) { jargs[pname] = v; } }
                                        scan = ve;
                                    }
                                    tcr.arguments_json = jargs.dump();
                                    result.tool_calls.push_back(std::move(tcr));
                                    pos = invoke_at + 1;
                                }
                                // Strip DSML tags from content so only clean text remains
                                size_t cp = 0;
                                std::string cleaned;
                                while (cp < c.size()) {
                                    auto nt = c.find("<" + ff5c + "DSML", cp);
                                    if (nt == std::string::npos) { cleaned += c.substr(cp); break; }
                                    cleaned += c.substr(cp, nt - cp);
                                    auto ne2 = c.find('>', nt + 1);
                                    if (ne2 == std::string::npos) { cp = nt + 1; continue; }
                                    cp = ne2 + 1;
                                }
                                if (!cleaned.empty()) result.content = cleaned;
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

        std::string extract_route_api_key(route::Route& r) const {
            if (r.auth.has_auth()) {
                route::AuthInput input{"POST", "", ""};
                auto result = r.auth(input);
                if (!result.has_error && !result.headers.authorization.empty()) {
                    auto& auth = result.headers.authorization;
                    if (auth.size() > 7) return auth.substr(7);
                }
            }
            return "";
        }
    };
}
