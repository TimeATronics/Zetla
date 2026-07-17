#pragma once
#include "../core/types.hpp"
#include "../core/provider.hpp"
#include "../tools/tool_registry.hpp"
#include "nlohmann/json.hpp"
#include <functional>
#include <string>
#include <vector>

namespace zetla::agentic {

    class AgenticLoop {
    public:
        AgenticLoop() = default;

        void set_tool_definitions(const std::vector<core::ToolDefinition>& defs) {
            tool_defs_ = defs;
        }

        void set_tool_executor(core::ToolExecutorCallback callback) {
            tool_executor_ = std::move(callback);
        }

        void add_tool_executor(core::IToolExecutor* exec) {
            builtin_executors_.push_back(exec);
        }

        void set_max_iterations(int n) {
            max_iterations_ = n;
        }

        void clear_tools() {
            tool_defs_.clear();
            tool_executor_ = nullptr;
            builtin_executors_.clear();
        }

        const std::vector<core::ToolDefinition>& tool_definitions() const {
            return tool_defs_;
        }

        std::string run(
            core::IProvider& provider,
            const core::LLMRequest& request,
            core::AgenticCallback callback
        ) {
            std::vector<core::Message> messages = request.messages;

            for (int iter = 0; iter < max_iterations_; ++iter) {
                core::LLMRequest req;
                req.model = request.model;
                req.messages = messages;
                req.generation = request.generation;
                req.tools = tool_defs_;

                core::SyncResponse resp = provider.generate_sync(req);

                if (resp.finish_reason == "error") {
                    callback(core::AgenticEvent{core::AgenticEvent::CONTENT, "Error: " + resp.content, "", "", true});
                    return resp.content;
                }

                if (resp.has_tool_calls && !resp.tool_calls.empty()) {
                    if (!resp.content.empty()) {
                        callback(core::AgenticEvent{core::AgenticEvent::CONTENT, resp.content, "", "", false});
                    }

                    core::Message assistant_msg;
                    assistant_msg.role = "assistant";
                    assistant_msg.content = resp.content.empty() ? "[Tool call]" : resp.content;
                    assistant_msg.tool_calls = resp.tool_calls;
                    messages.push_back(assistant_msg);

                    for (auto& tc : resp.tool_calls) {
                        callback(core::AgenticEvent{core::AgenticEvent::TOOL_CALL, tc.arguments_json, tc.name, tc.id, false});

                        core::ToolCallResult tool_result;
                        tool_result.tool_call_id = tc.id;
                        tool_result.is_error = false;

                        bool executed = false;
                        // Try builtin IToolExecutor instances first
                        for (auto* exec : builtin_executors_) {
                            if (exec && exec->name() == tc.name) {
                                core::ToolCallRequest treq = tc;
                                tool_result = exec->execute(treq);
                                tool_result.tool_call_id = tc.id;
                                executed = true;
                                break;
                            }
                        }
                        // Fall back to the global callback
                        if (!executed && tool_executor_) {
                            tool_result = tool_executor_(tc);
                            tool_result.tool_call_id = tc.id;
                            executed = true;
                        }

                        if (!executed) {
                            nlohmann::json err_j;
                            err_j["error"] = "Tool '" + tc.name + "' not found";
                            tool_result.content = err_j.dump();
                            tool_result.is_error = true;
                        }

                        callback(core::AgenticEvent{core::AgenticEvent::TOOL_RESULT, tool_result.content, tc.name, tc.id, false});

                        core::Message tool_msg;
                        tool_msg.role = "tool";
                        tool_msg.tool_call_id = tc.id;
                        tool_msg.content = tool_result.content;
                        messages.push_back(tool_msg);
                    }
                } else {
                    callback(core::AgenticEvent{core::AgenticEvent::CONTENT, resp.content, "", "", false});
                    callback(core::AgenticEvent{core::AgenticEvent::DONE, "", "", "", true});
                    return resp.content;
                }
            }

            callback(core::AgenticEvent{core::AgenticEvent::CONTENT, "[Max iterations reached]", "", "", false});
            callback(core::AgenticEvent{core::AgenticEvent::DONE, "", "", "", true});
            return "";
        }

    private:
        std::vector<core::ToolDefinition> tool_defs_;
        std::vector<core::IToolExecutor*> builtin_executors_;
        core::ToolExecutorCallback tool_executor_;
        int max_iterations_ = 10;
    };
}
