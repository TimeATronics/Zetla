#pragma once
#include "core/types.hpp"

namespace zetla::schema {
    using ContentPart = core::ContentPart;
    using Message = core::Message;
    using ChatOptions = core::ChatOptions;
    using ToolParameter = core::ToolParameter;
    using ToolDefinition = core::ToolDefinition;
    using ToolCall = core::ToolCallRequest;
    using ToolResult = core::ToolCallResult;
    using AgenticEvent = core::AgenticEvent;
    using LLMRequest = core::LLMRequest;
    using LLMEvent = core::LLMEvent;
    using LLMResponse = core::LLMResponse;
    using StreamChunk = core::StreamChunk;
    using SyncResponse = core::SyncResponse;
}
