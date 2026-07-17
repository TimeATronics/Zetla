#pragma once
#include "../core/types.hpp"
#include "nlohmann/json.hpp"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace zetla::tools {

    struct ToolContext {
        std::string id;
        std::string name;
    };

    struct ToolResult {
        std::string content;
        bool is_error = false;
    };

    struct DispatchResult {
        core::LLMEvent result_event;
        core::LLMEvent error_event;
        bool has_error = false;
    };

    using ToolHandler = std::function<ToolResult(const std::string& input_json, const ToolContext&)>;

    struct RegisteredTool {
        core::ToolDefinition definition;
        ToolHandler handler;
    };

    class ToolRegistry {
    public:
        void register_tool(core::ToolDefinition def, ToolHandler handler) {
            RegisteredTool rt;
            rt.definition = std::move(def);
            rt.handler = std::move(handler);
            tools_[rt.definition.name] = std::move(rt);
        }

        void register_executor(const std::string& name, core::ToolExecutorCallback executor) {
            executors_[name] = std::move(executor);
        }

        DispatchResult dispatch(const core::ToolCallRequest& call) {
            DispatchResult result;

            auto it = tools_.find(call.name);
            if (it != tools_.end() && it->second.handler) {
                ToolContext ctx{call.id, call.name};
                auto tool_result = it->second.handler(call.arguments_json, ctx);
                result.result_event = core::LLMEvent::tool_result(call.id, call.name, tool_result.content);
                result.has_error = tool_result.is_error;
                if (tool_result.is_error) {
                    result.error_event = core::LLMEvent::tool_error(call.id, call.name, tool_result.content);
                }
                return result;
            }

            auto exec_it = executors_.find(call.name);
            if (exec_it != executors_.end()) {
                core::ToolCallResult tc_result = exec_it->second(call);
                result.result_event = core::LLMEvent::tool_result(call.id, call.name, tc_result.content);
                result.has_error = tc_result.is_error;
                if (tc_result.is_error) {
                    result.error_event = core::LLMEvent::tool_error(call.id, call.name, tc_result.content);
                }
                return result;
            }

            nlohmann::json err_j;
            err_j["error"] = "Tool '" + call.name + "' not found";
            result.result_event = core::LLMEvent::tool_result(call.id, call.name, err_j.dump());
            result.error_event = core::LLMEvent::tool_error(call.id, call.name, "Tool not found: " + call.name);
            result.has_error = true;
            return result;
        }

        std::vector<core::ToolDefinition> definitions() const {
            std::vector<core::ToolDefinition> defs;
            for (auto& [name, tool] : tools_) {
                defs.push_back(tool.definition);
            }
            return defs;
        }

        bool has_tool(const std::string& name) const {
            return tools_.count(name) > 0 || executors_.count(name) > 0;
        }

        void clear() {
            tools_.clear();
            executors_.clear();
        }

    private:
        std::unordered_map<std::string, RegisteredTool> tools_;
        std::unordered_map<std::string, core::ToolExecutorCallback> executors_;
    };
}
