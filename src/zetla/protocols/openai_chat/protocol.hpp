#pragma once
#include "../../route/protocol.hpp"
#include "../../route/route.hpp"
#include "../../core/types.hpp"
#include "../../api/json_utils.hpp"
#include "nlohmann/json.hpp"
#include <vector>

namespace zetla::protocols::openai_chat {

    inline nlohmann::json build_message_json(const core::Message& msg) {
        nlohmann::json j;
        if (msg.role == "tool") {
            j["role"] = "tool";
            j["tool_call_id"] = msg.tool_call_id;
            j["content"] = msg.content;
        } else if (msg.has_tool_calls()) {
            j["role"] = "assistant";
            j["content"] = msg.content;
            nlohmann::json tc_arr = nlohmann::json::array();
            for (auto& tc : msg.tool_calls) {
                tc_arr.push_back({
                    {"id", tc.id},
                    {"type", "function"},
                    {"function", {
                        {"name", tc.name},
                        {"arguments", tc.arguments_json}
                    }}
                });
            }
            j["tool_calls"] = tc_arr;
        } else if (msg.is_multipart()) {
            j["role"] = msg.role;
            nlohmann::json parts = nlohmann::json::array();
            for (auto& p : msg.parts) {
                if (p.type == "text") {
                    parts.push_back({{"type", "text"}, {"text", p.text}});
                } else {
                    parts.push_back({{"type", "image_url"}, {"image_url", {{"url", p.image_url}}}});
                }
            }
            j["content"] = parts;
        } else {
            j["role"] = msg.role;
            j["content"] = msg.content;
        }
        return j;
    }

    inline std::string build_body(const core::LLMRequest& request, const route::RouteDefaults& defaults) {
        std::string model = request.model;

        if (defaults.model_prefix_to_strip.has_value()) {
            const auto& prefix = defaults.model_prefix_to_strip.value();
            if (model.size() > prefix.size() && model.compare(0, prefix.size(), prefix) == 0) {
                model = model.substr(prefix.size());
            }
        }

        nlohmann::json body;
        body["model"] = model;

        nlohmann::json messages = nlohmann::json::array();
        if (!request.system_prompt.empty()) {
            messages.push_back({{"role", "system"}, {"content", request.system_prompt}});
        }
        for (auto& msg : request.messages) {
            messages.push_back(build_message_json(msg));
        }
        body["messages"] = messages;

        if (!request.tools.empty()) {
            nlohmann::json tools = nlohmann::json::array();
            for (auto& t : request.tools) {
                nlohmann::json fn;
                fn["name"] = t.name;
                fn["description"] = t.description;
                if (!t.parameters_schema_json.empty()) {
                    fn["parameters"] = nlohmann::json::parse(t.parameters_schema_json);
                }
                tools.push_back({{"type", "function"}, {"function", fn}});
            }
            body["tools"] = tools;
        }

        if (request.generation.has_value()) {
            auto& gen = request.generation.value();
            if (gen.temperature.has_value()) body["temperature"] = gen.temperature.value();
            if (gen.max_tokens.has_value()) body["max_tokens"] = gen.max_tokens.value();
            if (gen.top_p.has_value()) body["top_p"] = gen.top_p.value();
            if (gen.top_k.has_value()) body["top_k"] = gen.top_k.value();
            if (gen.seed.has_value()) body["seed"] = gen.seed.value();
            if (gen.frequency_penalty.has_value() && defaults.supports_frequency_penalty)
                body["frequency_penalty"] = gen.frequency_penalty.value();
            if (gen.presence_penalty.has_value() && defaults.supports_presence_penalty)
                body["presence_penalty"] = gen.presence_penalty.value();
            if (gen.stop.has_value() && !gen.stop.value().empty()) {
                body["stop"] = gen.stop.value();
            }
        }

        if (defaults.supports_reasoning_effort) {
            auto it = request.provider_options.find("reasoning_effort");
            if (it != request.provider_options.end()) {
                body["reasoning_effort"] = it->second;
            }
        }

        if (defaults.supports_response_format) {
            auto it = request.provider_options.find("response_format");
            if (it != request.provider_options.end()) {
                if (it->second == "json_object") {
                    body["response_format"] = {{"type", "json_object"}};
                } else {
                    body["response_format"] = {{"type", "text"}};
                }
            }
        }

        if (defaults.reasoning_key.has_value() && !request.provider_options.empty()) {
            auto re = request.provider_options.find("reasoning_effort");
            if (re != request.provider_options.end()) {
                const auto& key = defaults.reasoning_key.value();
                const auto& val = re->second;
                if (val == "disabled") {
                    body[key] = {{"type", "disabled"}};
                } else if (!val.empty()) {
                    body[key] = {{"type", "enabled"}, {"reasoning_effort", val}};
                }
            }
        }

        return body.dump();
    }

    inline std::vector<core::LLMEvent> parse_sse_event(
        route::StreamState& state,
        const std::string& sse_data
    ) {
        std::vector<core::LLMEvent> events;

        auto j = zetla::json::try_parse(sse_data);
        if (!j) return events;

        if (!j->contains("choices") || !(*j)["choices"].is_array() || (*j)["choices"].empty()) return events;

        auto& choice = (*j)["choices"][0];
        if (!choice.contains("delta")) return events;
        auto& delta = choice["delta"];

        if (!state.step_started) {
            events.push_back(core::LLMEvent::step_start(state.step_index));
            state.step_started = true;
        }

        if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) {
            state.finish_reason = core::parse_finish_reason(choice["finish_reason"].get<std::string>());
        }

        if (delta.contains("reasoning_content") && delta["reasoning_content"].is_string()) {
            std::string text = delta["reasoning_content"].get<std::string>();
            if (!text.empty()) {
                if (!state.reasoning_started) {
                    events.push_back(core::LLMEvent::reasoning_start("reasoning-" + std::to_string(state.step_index)));
                    state.reasoning_started = true;
                }
                events.push_back(core::LLMEvent::reasoning_delta("reasoning-" + std::to_string(state.step_index), text));
                state.reasoning += text;
            }
        }

        if (delta.contains("content") && delta["content"].is_string()) {
            std::string text = delta["content"].get<std::string>();
            if (!text.empty()) {
                if (state.reasoning_started) {
                    events.push_back(core::LLMEvent::reasoning_end("reasoning-" + std::to_string(state.step_index)));
                    state.reasoning_started = false;
                }
                if (!state.text_started) {
                    events.push_back(core::LLMEvent::text_start("text-" + std::to_string(state.step_index)));
                    state.text_started = true;
                }
                events.push_back(core::LLMEvent::text_delta("text-" + std::to_string(state.step_index), text));
                state.content += text;
            }
        }

        if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
            for (auto& tc : delta["tool_calls"]) {
                std::string id = tc.value("id", "");
                std::string name;
                std::string args;
                if (tc.contains("function")) {
                    auto& fn = tc["function"];
                    name = fn.value("name", "");
                    args = fn.value("arguments", "");
                }

                if (!name.empty() && name != "null") {
                    if (state.current_tool_call_id != id) {
                        if (!state.pending_tool_json.empty() && state.in_tool_call) {
                            events.push_back(core::LLMEvent::tool_input_end(state.current_tool_call_id, state.current_tool_name));
                            events.push_back(core::LLMEvent::tool_call(state.current_tool_call_id, state.current_tool_name, state.pending_tool_json));
                        }
                        state.current_tool_call_id = id;
                        state.current_tool_name = name;
                        state.pending_tool_json.clear();
                        state.in_tool_call = true;
                        events.push_back(core::LLMEvent::tool_input_start(id, name));
                    }
                    if (!args.empty()) {
                        state.pending_tool_json += args;
                        events.push_back(core::LLMEvent::tool_input_delta(id, name, args));
                    }
                }
            }
        }

        if (j->contains("usage")) {
            auto& usage = (*j)["usage"];
            if (usage.contains("prompt_tokens")) state.total_input_tokens = usage["prompt_tokens"].get<int>();
            if (usage.contains("completion_tokens")) state.total_output_tokens = usage["completion_tokens"].get<int>();
            if (usage.contains("reasoning_tokens")) state.total_reasoning_tokens = usage["reasoning_tokens"].get<int>();
        }

        return events;
    }

    inline std::vector<core::LLMEvent> on_halt(route::StreamState& state) {
        std::vector<core::LLMEvent> events;

        if (state.in_tool_call && !state.pending_tool_json.empty()) {
            events.push_back(core::LLMEvent::tool_input_end(state.current_tool_call_id, state.current_tool_name));
            events.push_back(core::LLMEvent::tool_call(state.current_tool_call_id, state.current_tool_name, state.pending_tool_json));
        }

        if (state.text_started) {
            events.push_back(core::LLMEvent::text_end("text-" + std::to_string(state.step_index)));
        }
        if (state.reasoning_started) {
            events.push_back(core::LLMEvent::reasoning_end("reasoning-" + std::to_string(state.step_index)));
        }

        events.push_back(core::LLMEvent::step_finish(state.step_index, state.finish_reason));
        events.push_back(core::LLMEvent::finish(state.finish_reason));
        return events;
    }

    inline route::Protocol make_protocol() {
        route::Protocol p;
        p.id = "openai-chat";
        p.build_body = [](const core::LLMRequest& req) -> std::string {
            return build_body(req, route::RouteDefaults{});
        };
        p.parse_step = [](route::StreamState& state, const std::string& sse_data) {
            return parse_sse_event(state, sse_data);
        };
        return p;
    }
}
