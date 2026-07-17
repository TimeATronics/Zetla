#pragma once
#include "schema.hpp"
#include "core/provider.hpp"

namespace zetla::provider {
    using TokenCallback = core::TokenCallback;
    using SseCallback = core::SseCallback;
    using ToolExecutorCallback = core::ToolExecutorCallback;
    using IToolExecutor = core::IToolExecutor;
    using IProvider = core::IProvider;
}
