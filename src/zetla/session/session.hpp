#pragma once
#include "../core/types.hpp"
#include "../core/provider.hpp"
#include "../file_handlers/base/file_handler.hpp"
#include "memory.hpp"
#include <string>
#include <chrono>
#include <vector>
#include <memory>
#include <unordered_map>

namespace zetla::session {

    struct SpaceFile {
        std::string name;
        std::string path;
        int64_t added_at_ms = 0;
    };

    struct Session {
        std::string id;
        std::string model;
        std::string title;
        bool is_starred = false;
        bool is_space = false;
        memory::ChatHistory history;
        core::ChatOptions options;
        std::vector<core::ToolDefinition> tools;
        std::vector<std::unique_ptr<core::IToolExecutor>> tool_executors;
        core::ToolExecutorCallback tool_executor;
        std::unordered_map<std::string, file_handlers::RegisteredFile> registered_files;
        std::vector<SpaceFile> space_files;
        std::chrono::system_clock::time_point created_at;
        std::chrono::system_clock::time_point last_active;

        Session(std::string id_, std::string model_, std::string system_prompt,
                size_t max_context_tokens = 8192)
            : id(std::move(id_))
            , model(std::move(model_))
            , history(std::move(system_prompt), max_context_tokens)
            , options(core::ChatOptions::defaults())
            , created_at(std::chrono::system_clock::now())
            , last_active(created_at) {}
    };

}
