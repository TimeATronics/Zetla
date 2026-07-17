#pragma once
#include "../core/types.hpp"
#include "../core/error.hpp"
#include <string>
#include <functional>
#include <vector>

namespace zetla::route {

    using BodyBuilder = std::function<std::string(const core::LLMRequest&)>;

    struct StreamState {
        std::string buffer;
        std::string content;
        std::string reasoning;
        std::string current_tool_call_id;
        std::string current_tool_name;
        std::string pending_tool_json;
        bool in_tool_call = false;
        bool step_started = false;
        bool text_started = false;
        bool reasoning_started = false;
        int step_index = 0;
        core::FinishReason finish_reason = core::FinishReason::Unknown;
        int total_input_tokens = 0;
        int total_output_tokens = 0;
        int total_reasoning_tokens = 0;

        void reset() {
            content.clear();
            reasoning.clear();
            current_tool_call_id.clear();
            current_tool_name.clear();
            pending_tool_json.clear();
            in_tool_call = false;
            step_started = false;
            text_started = false;
            reasoning_started = false;
        }
    };

    using StreamStep = std::function<std::vector<core::LLMEvent>(StreamState&, const std::string& sse_data)>;

    struct Protocol {
        std::string id;
        BodyBuilder build_body;
        StreamStep parse_step;
    };
}
