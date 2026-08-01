#pragma once
#include "session.hpp"
#include "../core/provider.hpp"
#include "../tools/tool_registry.hpp"
#include "../storage/storage_manager.hpp"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>

namespace zetla::session {

    struct SessionInfo {
        std::string model;
        size_t message_count = 0;
        std::chrono::system_clock::time_point created_at;
        std::chrono::system_clock::time_point last_active;
    };

    struct CompactionInfo {
        std::string session_id;
        size_t estimated_tokens = 0;
        size_t max_context_tokens = 8192;
        bool needs_compaction = false;
        std::string compacted_summary;
        size_t history_turns = 0;
        size_t total_messages = 0;
    };

    class SessionManager {
    private:
        std::unordered_map<std::string, std::unique_ptr<Session>> sessions_;
        std::mutex sessions_mutex_;
        std::unique_ptr<storage::StorageManager> storage_;
        std::unique_ptr<core::IProvider> provider_;
        tools::ToolRegistry tool_registry_;

        std::string generate_id();
        bool save_to_storage(const std::string& session_id);
        bool compact_history_internal(Session* sess, std::string& error_out);
        core::LLMRequest build_request(Session* sess, const std::string& user_message);
        bool execute_and_collect(
            core::LLMRequest& req,
            core::TokenCallback callback,
            std::string& full_response
        );

    public:
        explicit SessionManager(const std::string& storage_path = "");
        void set_provider(std::unique_ptr<core::IProvider> p);
        std::string list_models();
        static std::string list_available_providers();

        bool delete_session(const std::string& session_id);

        std::string create_session(
            const std::string& model = "deepseek-v4-flash",
            const std::string& system_prompt = ""
        );

        std::string create_space(
            const std::string& model = "deepseek-v4-flash",
            const std::string& system_prompt = ""
        );

        bool send_message(
            const std::string& session_id,
            const std::string& user_message,
            core::TokenCallback callback,
            std::string& error_out
        );

        bool send_message_sse(
            const std::string& session_id,
            const std::string& user_message,
            core::SseCallback callback,
            std::string& error_out
        );

        bool send_message_multipart(
            const std::string& session_id,
            const core::Message& user_message,
            core::TokenCallback callback,
            std::string& error_out
        );

        bool send_message_multipart_sse(
            const std::string& session_id,
            const core::Message& user_message,
            core::SseCallback callback,
            std::string& error_out
        );

        bool send_message_agentic(
            const std::string& session_id,
            const std::string& user_message,
            core::AgenticCallback callback,
            std::string& error_out
        );

        bool set_session_options(
            const std::string& session_id,
            const core::ChatOptions& options
        );

        bool set_session_system_prompt(
            const std::string& session_id,
            const std::string& system_prompt
        );

        bool set_session_model(
            const std::string& session_id,
            const std::string& model
        );

        bool set_session_title(
            const std::string& session_id,
            const std::string& title
        );

        bool set_session_starred(
            const std::string& session_id,
            bool starred
        );

        core::ChatOptions get_session_options(const std::string& session_id);

        bool is_space(const std::string& session_id);

        bool add_space_file(const std::string& session_id, const std::string& file_path);
        std::vector<SpaceFile> list_space_files(const std::string& session_id);

        bool add_tool_to_session(
            const std::string& session_id,
            std::unique_ptr<core::IToolExecutor> tool
        );

        bool add_tool_definition(
            const std::string& session_id,
            const core::ToolDefinition& def
        );

        bool set_tool_executor(
            const std::string& session_id,
            core::ToolExecutorCallback callback
        );

        bool clear_history(const std::string& session_id);

        std::vector<core::Message> get_history(const std::string& session_id);

        SessionInfo get_session_info(const std::string& session_id);

        struct PersistedSessionInfo {
            std::string id;
            std::string model;
            std::string title;
            bool is_starred = false;
            bool is_space = false;
            long long created_at = 0;
            long long last_active = 0;
            std::string compacted_summary;
        };
        bool load_from_storage(const std::string& session_id);
        std::vector<std::string> list_persisted_sessions();
        std::vector<PersistedSessionInfo> list_persisted_sessions_full();
        bool delete_from_storage(const std::string& session_id);
        bool session_exists_on_disk(const std::string& session_id);

        bool compact_session(const std::string& session_id, std::string& error_out);
        CompactionInfo get_compaction_info(const std::string& session_id);

        std::string add_file(
            const std::string& session_id,
            const std::string& file_path,
            std::string& error_out
        );

        bool remove_file(
            const std::string& session_id,
            const std::string& file_id
        );

        std::vector<file_handlers::RegisteredFile> list_files(
            const std::string& session_id
        );

        bool send_message_with_files(
            const std::string& session_id,
            const std::string& user_message,
            const std::vector<std::string>& file_ids,
            core::TokenCallback callback,
            std::string& error_out
        );
    };

}
