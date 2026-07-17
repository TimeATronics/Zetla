#pragma once
#include <string>
#include <vector>
#include <optional>
#include <variant>
#include <cstdint>
#include <functional>

namespace zetla::core {

    struct ContentPart {
        std::string type;
        std::string text;
        std::string image_url;
        std::string media_type;
    };

    struct ToolCallRequest {
        std::string id;
        std::string name;
        std::string arguments_json;
    };

    struct ToolCallResult {
        std::string tool_call_id;
        std::string content;
        bool is_error = false;
    };

    struct Message {
        std::string role;
        std::string content;
        std::string tool_call_id;
        std::string name;
        std::vector<ToolCallRequest> tool_calls;
        std::vector<ContentPart> parts;

        bool is_multipart() const { return !parts.empty(); }
        bool has_tool_calls() const { return !tool_calls.empty(); }

        Message() = default;
        Message(std::string r, std::string c) : role(std::move(r)), content(std::move(c)) {}
        Message(std::string r, std::string c, std::string tci)
            : role(std::move(r)), content(std::move(c)), tool_call_id(std::move(tci)) {}
    };

    struct GenerationOptions {
        std::optional<int> max_tokens;
        std::optional<float> temperature;
        std::optional<float> top_p;
        std::optional<int> top_k;
        std::optional<float> frequency_penalty;
        std::optional<float> presence_penalty;
        std::optional<int> seed;
        std::optional<std::vector<std::string>> stop;

        static GenerationOptions defaults() {
            GenerationOptions o;
            o.temperature = 0.7f;
            return o;
        }
    };

    using ProviderOptions = std::unordered_map<std::string, std::string>;

    struct ChatOptions {
        GenerationOptions generation;
        ProviderOptions provider_options;

        std::optional<size_t> max_context_tokens;
        std::optional<bool> auto_compact;
        std::optional<std::string> compact_model;
        std::optional<bool> compact_on_save;

        static ChatOptions defaults() {
            ChatOptions o;
            o.generation = GenerationOptions::defaults();
            o.max_context_tokens = 8192;
            o.auto_compact = true;
            o.compact_on_save = true;
            return o;
        }
    };

    struct ToolParameter {
        std::string name;
        std::string type;
        std::string description;
        bool required = false;
        std::vector<std::string> enum_values;
    };

    struct ToolDefinition {
        std::string name;
        std::string description;
        std::vector<ToolParameter> parameters;
        std::string parameters_schema_json;
    };

    struct StreamChunk {
        std::string delta_content;
        std::string reasoning;
        bool is_finished = false;
    };

    struct SyncResponse {
        std::string content;
        std::string reasoning;
        std::string finish_reason;
        std::vector<ToolCallRequest> tool_calls;
        bool has_tool_calls = false;
    };

    struct AgenticEvent {
        enum Type { THINKING, TOOL_CALL, TOOL_RESULT, CONTENT, DONE };
        Type type;
        std::string data;
        std::string tool_name;
        std::string tool_call_id;
        bool is_finished = false;
    };

    enum class FinishReason {
        Stop,
        Length,
        ToolCalls,
        ContentFilter,
        Error,
        Unknown
    };

    inline const char* finish_reason_to_string(FinishReason r) {
        switch (r) {
            case FinishReason::Stop: return "stop";
            case FinishReason::Length: return "length";
            case FinishReason::ToolCalls: return "tool_calls";
            case FinishReason::ContentFilter: return "content_filter";
            case FinishReason::Error: return "error";
            case FinishReason::Unknown: return "unknown";
        }
        return "unknown";
    }

    inline FinishReason parse_finish_reason(const std::string& s) {
        if (s == "stop") return FinishReason::Stop;
        if (s == "length") return FinishReason::Length;
        if (s == "tool_calls") return FinishReason::ToolCalls;
        if (s == "content_filter") return FinishReason::ContentFilter;
        if (s == "error") return FinishReason::Error;
        return FinishReason::Unknown;
    }

    struct LLMEvent {
        enum class Type {
            StepStart,
            TextStart,
            TextDelta,
            TextEnd,
            ReasoningStart,
            ReasoningDelta,
            ReasoningEnd,
            ToolInputStart,
            ToolInputDelta,
            ToolInputEnd,
            ToolCall,
            ToolResult,
            ToolError,
            StepFinish,
            Finish,
            ProviderError
        };

        Type type;
        std::string text;
        std::string id;
        std::string name;
        std::string tool_call_id;
        std::string input_json;
        FinishReason reason = FinishReason::Unknown;
        int step_index = 0;
        bool retryable = false;

        static LLMEvent step_start(int index) {
            return {Type::StepStart, "", "", "", "", "", FinishReason::Unknown, index};
        }
        static LLMEvent text_start(const std::string& block_id) {
            return {Type::TextStart, "", block_id, "", "", ""};
        }
        static LLMEvent text_delta(const std::string& block_id, const std::string& text) {
            return {Type::TextDelta, text, block_id, "", "", ""};
        }
        static LLMEvent text_end(const std::string& block_id) {
            return {Type::TextEnd, "", block_id, "", "", ""};
        }
        static LLMEvent reasoning_start(const std::string& block_id) {
            return {Type::ReasoningStart, "", block_id, "", "", ""};
        }
        static LLMEvent reasoning_delta(const std::string& block_id, const std::string& text) {
            return {Type::ReasoningDelta, text, block_id, "", "", ""};
        }
        static LLMEvent reasoning_end(const std::string& block_id) {
            return {Type::ReasoningEnd, "", block_id, "", "", ""};
        }
        static LLMEvent tool_input_start(const std::string& call_id, const std::string& name) {
            return {Type::ToolInputStart, "", call_id, name, "", ""};
        }
        static LLMEvent tool_input_delta(const std::string& call_id, const std::string& name, const std::string& text) {
            return {Type::ToolInputDelta, text, call_id, name, "", ""};
        }
        static LLMEvent tool_input_end(const std::string& call_id, const std::string& name) {
            return {Type::ToolInputEnd, "", call_id, name, "", ""};
        }
        static LLMEvent tool_call(const std::string& call_id, const std::string& name, const std::string& input) {
            return {Type::ToolCall, "", call_id, name, "", input};
        }
        static LLMEvent tool_result(const std::string& call_id, const std::string& name, const std::string& result) {
            return {Type::ToolResult, result, call_id, name, ""};
        }
        static LLMEvent tool_error(const std::string& call_id, const std::string& name, const std::string& message) {
            return {Type::ToolError, message, call_id, name, ""};
        }
        static LLMEvent step_finish(int index, FinishReason reason) {
            return {Type::StepFinish, "", "", "", "", "", reason, index};
        }
        static LLMEvent finish(FinishReason reason) {
            return {Type::Finish, "", "", "", "", "", reason};
        }
        static LLMEvent provider_error(const std::string& message, bool retryable = false) {
            LLMEvent e;
            e.type = Type::ProviderError;
            e.text = message;
            e.retryable = retryable;
            return e;
        }
    };

    struct Usage {
        int input_tokens = 0;
        int output_tokens = 0;
        int reasoning_tokens = 0;
        int total_tokens = 0;
    };

    struct LLMResponse {
        std::string text;
        std::string reasoning;
        std::vector<ToolCallRequest> tool_calls;
        FinishReason finish_reason = FinishReason::Unknown;
        Usage usage;

        void reduce(const LLMEvent& event);
    };

    struct LLMRequest {
        std::string model;
        std::string system_prompt;
        std::vector<Message> messages;
        std::vector<ToolDefinition> tools;
        std::optional<GenerationOptions> generation;
        ProviderOptions provider_options;

        static LLMRequest from_legacy(
            const std::string& model,
            const std::vector<Message>& messages,
            const std::optional<ChatOptions>& options = std::nullopt,
            const std::vector<ToolDefinition>& tools = {}
        ) {
            LLMRequest req;
            req.model = model;
            req.messages = messages;
            req.tools = tools;
            if (options.has_value()) {
                req.generation = options->generation;
                req.provider_options = options->provider_options;
            }
            return req;
        }
    };

    using TokenCallback = std::function<void(const StreamChunk&)>;
    using SseCallback = std::function<void(const std::string& json_data, bool is_done)>;
    using ToolExecutorCallback = std::function<ToolCallResult(const ToolCallRequest&)>;
    using AgenticCallback = std::function<void(const AgenticEvent&)>;

    class IToolExecutor {
    public:
        virtual ~IToolExecutor() = default;
        virtual std::string name() const = 0;
        virtual std::string description() const = 0;
        virtual std::string parameters_schema() const = 0;
        virtual ToolCallResult execute(const ToolCallRequest& call) = 0;
    };

    class IProvider {
    public:
        virtual ~IProvider() = default;
        virtual void generate_stream(const LLMRequest& request, TokenCallback callback) = 0;
        virtual void generate_stream_sse(const LLMRequest& request, SseCallback callback) = 0;
        virtual SyncResponse generate_sync(const LLMRequest& request) = 0;
        virtual std::string list_models() = 0;
        virtual std::string provider_id() const = 0;
    };
}
