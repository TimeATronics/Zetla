#include "session_manager.hpp"
#include "../core/config.hpp"
#include "../network/http_client.hpp"
#include "../providers/registry.hpp"
#include "../agentic/agentic_loop.hpp"
#include "../api/json_utils.hpp"
#include "../storage/local/file_storage_backend.hpp"
#include "../file_handlers/base/file_handler_factory.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <random>
#include <set>
#include <sstream>
#include <iomanip>

namespace zetla::session {

    std::string SessionManager::generate_id() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);

        std::stringstream ss;
        for (int i = 0; i < 32; ++i) {
            ss << std::hex << dis(gen);
        }
        return ss.str();
    }

    SessionManager::SessionManager(const std::string& storage_path) {
        if (!storage_path.empty()) {
            auto backend = std::make_unique<storage::FileStorageBackend>(storage_path);
            storage_ = std::make_unique<storage::StorageManager>(std::move(backend));
        }
    }

    void SessionManager::set_provider(std::unique_ptr<core::IProvider> p) {
        if (p) provider_ = std::move(p);
    }

    std::string SessionManager::list_available_providers() {
        return providers::list_available_providers();
    }

    std::string SessionManager::list_models() {
        if (!provider_) return "[]";
        return provider_->list_models();
    }

    static storage::StoredSession session_to_stored(const Session& s) {
        storage::StoredSession stored;
        stored.id = s.id;
        stored.model = s.model;
        stored.title = s.title;
        stored.is_starred = s.is_starred;
        stored.is_space = s.is_space;
        stored.system_prompt = s.history.system_prompt();
        stored.compacted_summary = s.history.compacted_summary();

        auto msgs = s.history.history_snapshot();
        for (auto& m : msgs) {
            stored.messages.push_back({m.role, m.content});
        }

        nlohmann::json opts_j;
        auto& gen = s.options.generation;
        if (gen.temperature.has_value()) opts_j["temperature"] = core::GenerationOptions::rounded(gen.temperature.value());
        if (gen.max_tokens.has_value()) opts_j["max_tokens"] = gen.max_tokens.value();
        if (gen.top_p.has_value()) opts_j["top_p"] = core::GenerationOptions::rounded(gen.top_p.value());
        if (gen.frequency_penalty.has_value()) opts_j["frequency_penalty"] = gen.frequency_penalty.value();
        if (gen.presence_penalty.has_value()) opts_j["presence_penalty"] = gen.presence_penalty.value();
        if (gen.seed.has_value()) opts_j["seed"] = gen.seed.value();
        if (s.options.max_context_tokens.has_value()) opts_j["max_context_tokens"] = s.options.max_context_tokens.value();
        if (s.options.auto_compact.has_value()) opts_j["auto_compact"] = s.options.auto_compact.value();
        if (s.options.compact_model.has_value()) opts_j["compact_model"] = s.options.compact_model.value();
        if (s.options.compact_on_save.has_value()) opts_j["compact_on_save"] = s.options.compact_on_save.value();
        for (auto& [k, v] : s.options.provider_options) opts_j[k] = v;
        stored.options_json = opts_j.dump();

        // Serialize space_files
        if (!s.space_files.empty()) {
            nlohmann::json sf_j = nlohmann::json::array();
            for (auto& sf : s.space_files) {
                nlohmann::json f;
                f["name"] = sf.name;
                f["path"] = sf.path;
                f["added_at_ms"] = sf.added_at_ms;
                sf_j.push_back(f);
            }
            stored.space_files_json = sf_j.dump();
        }

        auto to_ms = [](std::chrono::system_clock::time_point tp) -> int64_t {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                tp.time_since_epoch()).count();
        };
        stored.created_at_ms = to_ms(s.created_at);
        stored.last_active_ms = to_ms(s.last_active);

        return stored;
    }

    static void restore_session_from_stored(Session& s, const storage::StoredSession& stored) {
        s.id = stored.id;
        s.model = stored.model;
        s.title = stored.title;
        s.is_starred = stored.is_starred;
        s.is_space = stored.is_space;

        s.history.clear();
        s.history.set_system_prompt(stored.system_prompt);

        // Restore space files
        s.space_files.clear();
        if (!stored.space_files_json.empty()) {
            try {
                auto sf_j = nlohmann::json::parse(stored.space_files_json);
                if (sf_j.is_array()) {
                    for (auto& f : sf_j) {
                        SpaceFile sf;
                        sf.name = f.value("name", "");
                        sf.path = f.value("path", "");
                        sf.added_at_ms = f.value("added_at_ms", (int64_t)0);
                        s.space_files.push_back(sf);
                    }
                }
            } catch (...) {}
        }

        if (!stored.compacted_summary.empty()) {
            s.history.set_compacted_summary(stored.compacted_summary);
            size_t keep_count = std::min(stored.messages.size(), size_t(4));
            size_t start = stored.messages.size() - keep_count;
            for (size_t i = start; i < stored.messages.size(); i++) {
                s.history.add_message(stored.messages[i].first, stored.messages[i].second);
            }
        } else {
            for (auto& m : stored.messages) {
                s.history.add_message(m.first, m.second);
            }
        }

        if (!stored.options_json.empty()) {
            s.options = core::ChatOptions::defaults();
            try {
                auto j = nlohmann::json::parse(stored.options_json);
                if (j.contains("temperature")) s.options.generation.temperature = j["temperature"].get<float>();
                if (j.contains("max_tokens")) s.options.generation.max_tokens = j["max_tokens"].get<int>();
                if (j.contains("top_p")) s.options.generation.top_p = j["top_p"].get<float>();
                if (j.contains("frequency_penalty")) s.options.generation.frequency_penalty = j["frequency_penalty"].get<float>();
                if (j.contains("presence_penalty")) s.options.generation.presence_penalty = j["presence_penalty"].get<float>();
                if (j.contains("seed")) s.options.generation.seed = j["seed"].get<int>();
                if (j.contains("max_context_tokens")) {
                    size_t max_ctx = j["max_context_tokens"].get<size_t>();
                    s.options.max_context_tokens = max_ctx;
                    s.history.set_max_context_tokens(max_ctx);
                }
                if (j.contains("auto_compact")) s.options.auto_compact = j["auto_compact"].get<bool>();
                if (j.contains("compact_model")) s.options.compact_model = j["compact_model"].get<std::string>();
                if (j.contains("compact_on_save")) s.options.compact_on_save = j["compact_on_save"].get<bool>();

                // Restore provider_options - any string value that isn't a known option key
                static const std::set<std::string> known_keys = {
                    "temperature", "max_tokens", "top_p", "frequency_penalty",
                    "presence_penalty", "seed", "max_context_tokens",
                    "auto_compact", "compact_model", "compact_on_save"
                };
                for (auto& [k, v] : j.items()) {
                    if (known_keys.count(k) == 0 && v.is_string()) {
                        s.options.provider_options[k] = v.get<std::string>();
                    }
                }
            } catch (...) {}
        }

        auto from_ms = [](int64_t ms) -> std::chrono::system_clock::time_point {
            return std::chrono::system_clock::time_point(
                std::chrono::milliseconds(ms));
        };
        s.created_at = from_ms(stored.created_at_ms);
        s.last_active = from_ms(stored.last_active_ms);
    }

    bool SessionManager::save_to_storage(const std::string& session_id) {
        if (!storage_) return false;
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return false;
        auto stored = session_to_stored(*it->second);
        ZLOGI("[session] save: id=%s is_space=%d title=%s", session_id.c_str(), (int)stored.is_space, stored.title.c_str());
        return storage_->save_session(stored);
    }

    bool SessionManager::compact_history_internal(Session* sess, std::string& error_out) {
        auto old_msgs = sess->history.messages_to_compact();
        if (old_msgs.empty()) return true;

        auto keep_msgs = sess->history.messages_to_keep();
        std::string existing_summary = sess->history.compacted_summary();

        std::ostringstream compaction_input;
        if (!existing_summary.empty()) {
            compaction_input << "Previous summary:\n" << existing_summary << "\n\n";
        }
        compaction_input << "Conversation to summarize:\n";
        for (auto& m : old_msgs) {
            compaction_input << m.role << ": " << m.content << "\n";
        }

        std::string compaction_prompt =
            "Summarize the following conversation concisely. "
            "Include all key facts, decisions, and context. "
            "Keep it under 200 tokens. "
            "Output ONLY the summary text, nothing else.\n\n"
            + compaction_input.str();

        core::LLMRequest req;
        req.model = sess->options.compact_model.value_or(sess->model);
        req.system_prompt = "You are a conversation summarizer. Output only the summary.";
        req.messages.push_back({"user", compaction_prompt});
        req.generation = core::GenerationOptions{};
        req.generation->temperature = 0.3f;
        req.generation->max_tokens = 512;

        if (!provider_) {
            error_out = "No provider configured for compaction";
            return false;
        }

        std::string summary;
        provider_->generate_stream(req,
            [&](const core::StreamChunk& chunk) {
                if (!chunk.is_finished) {
                    summary += chunk.delta_content;
                }
            });

        if (summary.empty()) {
            error_out = "Compaction returned empty summary";
            return false;
        }

        std::string new_summary;
        if (!existing_summary.empty()) {
            new_summary = existing_summary + "\n\n" + summary;
        } else {
            new_summary = summary;
        }

        if (new_summary.size() > 2000) {
            new_summary = new_summary.substr(new_summary.size() - 2000);
        }

        sess->history.apply_compaction(new_summary, keep_msgs);
        return true;
    }

    core::LLMRequest SessionManager::build_request(Session* sess, const std::string& user_message) {
        core::LLMRequest req;
        req.model = sess->model;
        req.system_prompt = sess->history.system_prompt();
        req.messages = sess->history.build_payload();
        req.generation = sess->options.generation;
        req.provider_options = sess->options.provider_options;
        req.tools = sess->tools;
        return req;
    }

    bool SessionManager::execute_and_collect(
        core::LLMRequest& req,
        core::TokenCallback callback,
        std::string& full_response
    ) {
        if (!provider_) return false;

        provider_->generate_stream(req,
            [&](const core::StreamChunk& chunk) {
                if (!chunk.is_finished) {
                    full_response += chunk.delta_content;
                }
                if (chunk.is_finished) {
                    ZLOGI("[LLM] stream finished: total=%zu chars reasoning=%zu chars",
                        full_response.size(), chunk.reasoning.size());
                    ZLOGI("[LLM] response (first 500): %s", log::truncate(full_response, 500).c_str());
                }
                callback(chunk);
            });

        return true;
    }

    std::string SessionManager::create_session(const std::string& model, const std::string& system_prompt) {
        std::string id;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            id = generate_id();
            sessions_[id] = std::make_unique<Session>(id, model, system_prompt);
        }
        save_to_storage(id);
        return id;
    }

    std::string SessionManager::create_space(const std::string& model, const std::string& system_prompt) {
        std::string id;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            id = generate_id();
            auto sess = std::make_unique<Session>(id, model, system_prompt);
            sess->is_space = true;
            sessions_[id] = std::move(sess);
        }
        save_to_storage(id);
        return id;
    }

    bool SessionManager::is_space(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        return it != sessions_.end() && it->second->is_space;
    }

    bool SessionManager::add_space_file(const std::string& session_id, const std::string& file_path) {
        std::string sid(session_id);
        SpaceFile sf;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(session_id);
            if (it == sessions_.end()) return false;

            std::string name = file_path;
            auto slash = file_path.find_last_of("/\\");
            if (slash != std::string::npos) name = file_path.substr(slash + 1);

            sf.name = name;
            sf.path = file_path;
            sf.added_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            it->second->space_files.push_back(sf);
        }
        save_to_storage(sid);
        return true;
    }

    std::vector<SpaceFile> SessionManager::list_space_files(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return {};
        return it->second->space_files;
    }

    bool SessionManager::delete_session(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        return sessions_.erase(session_id) > 0;
    }

    bool SessionManager::send_message(
        const std::string& session_id,
        const std::string& user_message,
        core::TokenCallback callback,
        std::string& error_out
    ) {
        if (!provider_) {
            error_out = "No provider configured";
            return false;
        }

        Session* sess = nullptr;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(session_id);
            if (it == sessions_.end()) {
                error_out = "Session not found";
                return false;
            }
            sess = it->second.get();
        }

        sess->history.add_message("user", user_message);

        if (sess->options.auto_compact.value_or(true) && sess->history.needs_compaction()) {
            std::string cerr;
            compact_history_internal(sess, cerr);
        }

        auto req = build_request(sess, user_message);
        std::string full_response;

        if (!sess->tools.empty()) {
            // Tool state machine: iterate until LLM stops calling tools or max iterations reached
            int tool_iterations = 0;
            const int max_tool_iterations = 20;
            req.tools = sess->tools;

            std::vector<core::Message> msgs = req.messages;

            while (tool_iterations < max_tool_iterations) {
                // Check abort flag between iterations
                if (network::HttpClient::is_cancelled()) {
                    break;
                }

                core::LLMRequest current_req;
                current_req.model = req.model;
                current_req.system_prompt = req.system_prompt;
                current_req.messages = msgs;
                current_req.generation = req.generation;
                current_req.provider_options = req.provider_options;
                current_req.tools = sess->tools;

                core::SyncResponse resp = provider_->generate_sync(current_req);

                if (resp.finish_reason == "error") {
                    full_response = "Error: " + resp.content;
                    break;
                }

                ZLOGI("[LLM] sync response: finish=%s reasoning=%zu chars content=%zu chars%s",
                    resp.finish_reason.c_str(), resp.reasoning.size(), resp.content.size(),
                    resp.has_tool_calls ? (" tools=" + std::to_string(resp.tool_calls.size())).c_str() : "");
                if (!resp.content.empty()) {
                    ZLOGI("[LLM] content (first 400): %s", log::truncate(resp.content, 400).c_str());
                }

                // Send reasoning to UI thinking block
                if (!resp.reasoning.empty()) {
                    callback({"", resp.reasoning, false});
                }

                if (resp.has_tool_calls && !resp.tool_calls.empty()) {
                    core::Message assistant_msg;
                    assistant_msg.role = "assistant";
                    assistant_msg.content = resp.content.empty() ? "[Tool call]" : resp.content;
                    assistant_msg.tool_calls = resp.tool_calls;
                    msgs.push_back(assistant_msg);

                    for (auto& tc : resp.tool_calls) {
                        ZLOGI("[AGENT] tool_call: name=%s id=%s args=%s",
                            tc.name.c_str(), tc.id.c_str(), log::truncate(tc.arguments_json, 500).c_str());

                        core::ToolCallResult tcr;
                        tcr.tool_call_id = tc.id;

                        auto builtin = std::find_if(sess->tool_executors.begin(), sess->tool_executors.end(),
                            [&](auto& exec) { return exec->name() == tc.name; });
                        if (builtin != sess->tool_executors.end()) {
                            tcr = (*builtin)->execute(core::ToolCallRequest{tc.id, tc.name, tc.arguments_json});
                            ZLOGI("[AGENT] tool_result: name=%s success=%d content_len=%zu",
                                tc.name.c_str(), !tcr.is_error, tcr.content.size());
                        } else if (sess->tool_executor) {
                            tcr = sess->tool_executor(core::ToolCallRequest{tc.id, tc.name, tc.arguments_json});
                            ZLOGI("[AGENT] tool_result: name=%s success=%d content_len=%zu",
                                tc.name.c_str(), !tcr.is_error, tcr.content.size());
                        } else {
                            tcr.content = "{}";
                            tcr.is_error = true;
                            ZLOGW("[AGENT] tool_result: name=%s NOT_FOUND (no executor registered)", tc.name.c_str());
                        }

                        if (!tcr.is_error) {
                            ZLOGI("[AGENT] tool_output (first 300): %s", log::truncate(tcr.content, 300).c_str());
                        } else {
                            ZLOGE("[AGENT] tool_error: name=%s error=%s", tc.name.c_str(), log::truncate(tcr.content, 300).c_str());
                        }

                        core::Message tool_msg;
                        tool_msg.role = "tool";
                        tool_msg.tool_call_id = tcr.tool_call_id;
                        tool_msg.content = tcr.is_error ? "Error: " + tcr.content : tcr.content;
                        msgs.push_back(tool_msg);
                    }

                    ZLOGI("[AGENT] iteration %d/%d complete", tool_iterations + 1, max_tool_iterations);
                    tool_iterations++;
                } else {
                    full_response = resp.content;
                    break;
                }
            }

            if (tool_iterations >= max_tool_iterations && full_response.empty()) {
                full_response = "I couldn't complete that request with the available tools.";
            }

            // Stream final result through callback
            if (!full_response.empty()) {
                callback({full_response, "", false});
            }
            callback({"", "", true});
        } else {
            // No tools: use existing streaming path (already handles callbacks)
            execute_and_collect(req, callback, full_response);
        }

        sess->history.add_message("assistant", full_response);
        sess->last_active = std::chrono::system_clock::now();

        if (storage_) {
            auto stored = session_to_stored(*sess);
            storage_->save_session(stored);
        }

        return true;
    }

    bool SessionManager::send_message_sse(
        const std::string& session_id,
        const std::string& user_message,
        core::SseCallback callback,
        std::string& error_out
    ) {
        if (!provider_) {
            error_out = "No provider configured";
            return false;
        }

        Session* sess = nullptr;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(session_id);
            if (it == sessions_.end()) {
                error_out = "Session not found";
                return false;
            }
            sess = it->second.get();
        }

        sess->history.add_message("user", user_message);

        if (sess->options.auto_compact.value_or(true) && sess->history.needs_compaction()) {
            std::string cerr;
            compact_history_internal(sess, cerr);
        }

        auto req = build_request(sess, user_message);
        std::string full_response;

        provider_->generate_stream_sse(req,
            [&](const std::string& json_data, bool is_done) {
                if (!is_done && !json_data.empty()) {
                    std::string content, reasoning;
                    json::extract_delta_content(json_data, content, reasoning);
                    full_response += content;
                }
                callback(json_data, is_done);
            });

        sess->history.add_message("assistant", full_response);
        sess->last_active = std::chrono::system_clock::now();

        if (storage_) {
            auto stored = session_to_stored(*sess);
            storage_->save_session(stored);
        }

        return true;
    }

    bool SessionManager::send_message_multipart(
        const std::string& session_id,
        const core::Message& user_message,
        core::TokenCallback callback,
        std::string& error_out
    ) {
        if (!provider_) {
            error_out = "No provider configured";
            return false;
        }

        Session* sess = nullptr;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(session_id);
            if (it == sessions_.end()) {
                error_out = "Session not found";
                return false;
            }
            sess = it->second.get();
        }

        sess->history.add_message(user_message.role, user_message.content);

        if (sess->options.auto_compact.value_or(true) && sess->history.needs_compaction()) {
            std::string cerr;
            compact_history_internal(sess, cerr);
        }

        auto req = build_request(sess, user_message.content);
        req.messages.back() = user_message;

        std::string full_response;
        execute_and_collect(req, callback, full_response);

        sess->history.add_message("assistant", full_response);
        sess->last_active = std::chrono::system_clock::now();

        if (storage_) {
            auto stored = session_to_stored(*sess);
            storage_->save_session(stored);
        }

        return true;
    }

    bool SessionManager::send_message_multipart_sse(
        const std::string& session_id,
        const core::Message& user_message,
        core::SseCallback callback,
        std::string& error_out
    ) {
        if (!provider_) {
            error_out = "No provider configured";
            return false;
        }

        Session* sess = nullptr;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(session_id);
            if (it == sessions_.end()) {
                error_out = "Session not found";
                return false;
            }
            sess = it->second.get();
        }

        sess->history.add_message(user_message.role, user_message.content);

        if (sess->options.auto_compact.value_or(true) && sess->history.needs_compaction()) {
            std::string cerr;
            compact_history_internal(sess, cerr);
        }

        auto req = build_request(sess, user_message.content);
        req.messages.back() = user_message;

        std::string full_response;

        provider_->generate_stream_sse(req,
            [&](const std::string& json_data, bool is_done) {
                if (!is_done && !json_data.empty()) {
                    std::string content, reasoning;
                    json::extract_delta_content(json_data, content, reasoning);
                    full_response += content;
                }
                callback(json_data, is_done);
            });

        sess->history.add_message("assistant", full_response);
        sess->last_active = std::chrono::system_clock::now();

        if (storage_) {
            auto stored = session_to_stored(*sess);
            storage_->save_session(stored);
        }

        return true;
    }

    bool SessionManager::send_message_agentic(
        const std::string& session_id,
        const std::string& user_message,
        core::AgenticCallback callback,
        std::string& error_out
    ) {
        if (!provider_) {
            error_out = "No provider configured";
            return false;
        }

        Session* sess = nullptr;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(session_id);
            if (it == sessions_.end()) {
                error_out = "Session not found";
                return false;
            }
            sess = it->second.get();
        }

        sess->history.add_message("user", user_message);

        if (sess->options.auto_compact.value_or(true) && sess->history.needs_compaction()) {
            std::string cerr;
            compact_history_internal(sess, cerr);
        }

        agentic::AgenticLoop loop;
        loop.set_max_iterations(sess->is_space ? 20 : 10);
        loop.set_tool_definitions(sess->tools);
        for (auto& exec : sess->tool_executors) {
            loop.add_tool_executor(exec.get());
        }
        if (sess->tool_executor) {
            loop.set_tool_executor(sess->tool_executor);
        }

        auto req = build_request(sess, user_message);
        req.tools = sess->tools;

        std::string final_response = loop.run(*provider_, req, callback);

        sess->history.add_message("assistant", final_response);
        sess->last_active = std::chrono::system_clock::now();

        if (storage_) {
            auto stored = session_to_stored(*sess);
            storage_->save_session(stored);
        }

        return true;
    }

    bool SessionManager::set_session_options(
        const std::string& session_id,
        const core::ChatOptions& options
    ) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return false;
        it->second->options = options;
        if (options.max_context_tokens.has_value()) {
            it->second->history.set_max_context_tokens(options.max_context_tokens.value());
        }
        return true;
    }

    core::ChatOptions SessionManager::get_session_options(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return core::ChatOptions::defaults();
        return it->second->options;
    }

    bool SessionManager::set_session_system_prompt(
        const std::string& session_id,
        const std::string& system_prompt
    ) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return false;
        it->second->history.set_system_prompt(system_prompt);
        return true;
    }

    bool SessionManager::set_session_model(
        const std::string& session_id,
        const std::string& model
    ) {
        std::string sid = session_id;
        std::string new_model = model;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(sid);
            if (it == sessions_.end()) return false;
            it->second->model = new_model;
        }
        save_to_storage(sid);
        return true;
    }

    bool SessionManager::set_session_title(
        const std::string& session_id,
        const std::string& title
    ) {
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(session_id);
            if (it == sessions_.end()) return false;
            it->second->title = title;
        }
        save_to_storage(session_id);
        return true;
    }

    bool SessionManager::set_session_starred(
        const std::string& session_id,
        bool starred
    ) {
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(session_id);
            if (it == sessions_.end()) return false;
            it->second->is_starred = starred;
        }
        save_to_storage(session_id);
        return true;
    }

    bool SessionManager::clear_history(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return false;
        it->second->history.clear();
        return true;
    }

    std::vector<core::Message> SessionManager::get_history(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return {};
        return it->second->history.build_payload();
    }

    SessionInfo SessionManager::get_session_info(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return {};

        SessionInfo info;
        info.model = it->second->model;
        info.created_at = it->second->created_at;
        info.last_active = it->second->last_active;

        auto msgs = it->second->history.build_payload();
        info.message_count = msgs.size();

        return info;
    }

    bool SessionManager::load_from_storage(const std::string& session_id) {
        if (!storage_) return false;

        storage::StoredSession stored;
        if (!storage_->load_session(session_id, stored)) return false;

        ZLOGI("[session] load: id=%s is_space=%d title=%s msgs=%zu", session_id.c_str(), (int)stored.is_space, stored.title.c_str(), stored.messages.size());

        std::lock_guard<std::mutex> lock(sessions_mutex_);
        size_t max_ctx = 8192;
        if (!stored.options_json.empty()) {
            auto val = zetla::json::extract_string(stored.options_json, "max_context_tokens");
            if (!val.empty()) {
                try { max_ctx = static_cast<size_t>(std::stoull(val)); } catch (...) {}
            }
        }
        auto sess = std::make_unique<Session>(stored.id, stored.model, stored.system_prompt, max_ctx);
        restore_session_from_stored(*sess, stored);
        sessions_[session_id] = std::move(sess);
        return true;
    }

    std::vector<std::string> SessionManager::list_persisted_sessions() {
        if (!storage_) return {};
        return storage_->list_session_ids();
    }

    std::vector<SessionManager::PersistedSessionInfo> SessionManager::list_persisted_sessions_full() {
        std::vector<PersistedSessionInfo> result;
        if (!storage_) return result;
        auto ids = storage_->list_session_ids();
        for (auto& id : ids) {
            storage::StoredSession stored;
            if (storage_->load_session(id, stored)) {
                PersistedSessionInfo info;
                info.id = stored.id;
                info.model = stored.model;
                info.title = stored.title;
                info.is_starred = stored.is_starred;
                info.is_space = stored.is_space;
                info.created_at = stored.created_at_ms;
                info.last_active = stored.last_active_ms;
                info.compacted_summary = stored.compacted_summary;
                result.push_back(std::move(info));
            }
        }
        return result;
    }

    bool SessionManager::delete_from_storage(const std::string& session_id) {
        if (!storage_) return false;
        return storage_->delete_session(session_id);
    }

    bool SessionManager::session_exists_on_disk(const std::string& session_id) {
        if (!storage_) return false;
        return storage_->session_exists(session_id);
    }

    bool SessionManager::compact_session(const std::string& session_id, std::string& error_out) {
        Session* sess = nullptr;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(session_id);
            if (it == sessions_.end()) {
                error_out = "Session not found";
                return false;
            }
            sess = it->second.get();
        }
        return compact_history_internal(sess, error_out);
    }

    CompactionInfo SessionManager::get_compaction_info(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        CompactionInfo info;
        if (it == sessions_.end()) return info;

        info.session_id = session_id;
        info.estimated_tokens = it->second->history.estimated_tokens();
        info.max_context_tokens = it->second->history.max_context_tokens();
        info.needs_compaction = it->second->history.needs_compaction();
        info.compacted_summary = it->second->history.compacted_summary();
        info.history_turns = it->second->history.message_count() / 2;
        info.total_messages = it->second->history.message_count();
        return info;
    }

    bool SessionManager::add_tool_to_session(
        const std::string& session_id,
        std::unique_ptr<core::IToolExecutor> tool
    ) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return false;

        core::ToolDefinition def;
        def.name = tool->name();
        def.description = tool->description();
        def.parameters_schema_json = tool->parameters_schema();
        it->second->tools.push_back(std::move(def));
        it->second->tool_executors.push_back(std::move(tool));
        return true;
    }

    bool SessionManager::add_tool_definition(
        const std::string& session_id,
        const core::ToolDefinition& def
    ) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return false;
        it->second->tools.push_back(def);
        return true;
    }

    bool SessionManager::set_tool_executor(
        const std::string& session_id,
        core::ToolExecutorCallback callback
    ) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return false;
        it->second->tool_executor = std::move(callback);
        return true;
    }

    std::string SessionManager::add_file(
        const std::string& session_id,
        const std::string& file_path,
        std::string& error_out
    ) {
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            if (sessions_.find(session_id) == sessions_.end()) {
                error_out = "Session not found";
                return "";
            }
        }

        std::string mime = file_handlers::detect_mime_type(file_path);
        auto handler = file_handlers::create_handler(mime);
        if (!handler) {
            error_out = "No handler for MIME type: " + mime;
            return "";
        }

        auto content = handler->extract(file_path);
        if (content.type == file_handlers::FileContentType::UNSUPPORTED) {
            error_out = "Failed to extract content from file: " + file_path;
            return "";
        }

        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);
        std::stringstream id_ss;
        id_ss << "file_";
        for (int i = 0; i < 12; ++i) id_ss << std::hex << dis(gen);
        std::string file_id = id_ss.str();

        file_handlers::RegisteredFile rf;
        rf.file_id = file_id;
        rf.path = file_path;
        rf.name = file_handlers::extract_file_name(file_path);
        rf.mime_type = mime;
        rf.content = std::move(content);
        rf.size_bytes = rf.content.size_bytes;

        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(session_id);
            if (it == sessions_.end()) {
                error_out = "Session not found";
                return "";
            }
            it->second->registered_files[file_id] = std::move(rf);
        }

        return file_id;
    }

    bool SessionManager::remove_file(
        const std::string& session_id,
        const std::string& file_id
    ) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return false;
        return it->second->registered_files.erase(file_id) > 0;
    }

    std::vector<file_handlers::RegisteredFile> SessionManager::list_files(
        const std::string& session_id
    ) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) return {};
        std::vector<file_handlers::RegisteredFile> result;
        for (auto& [id, rf] : it->second->registered_files) {
            result.push_back(rf);
        }
        return result;
    }

    bool SessionManager::send_message_with_files(
        const std::string& session_id,
        const std::string& user_message,
        const std::vector<std::string>& file_ids,
        core::TokenCallback callback,
        std::string& error_out
    ) {
        if (!provider_) {
            error_out = "No provider configured";
            return false;
        }

        Session* sess = nullptr;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(session_id);
            if (it == sessions_.end()) {
                error_out = "Session not found";
                return false;
            }
            sess = it->second.get();
        }

        bool has_images = false;
        std::vector<file_handlers::RegisteredFile*> resolved_files;
        for (auto& fid : file_ids) {
            auto it = sess->registered_files.find(fid);
            if (it == sess->registered_files.end()) continue;
            resolved_files.push_back(&it->second);
            if (it->second.content.type == file_handlers::FileContentType::IMAGE)
                has_images = true;
        }

        core::Message msg;
        bool use_multipart = has_images && !resolved_files.empty();

        if (use_multipart) {
            msg.role = "user";
            core::ContentPart text_part;
            text_part.type = "text";
            text_part.text = user_message;
            msg.parts.push_back(std::move(text_part));

            for (auto* rf : resolved_files) {
                if (rf->content.type == file_handlers::FileContentType::IMAGE) {
                    core::ContentPart img_part;
                    img_part.type = "image_url";
                    img_part.image_url = rf->content.image_data_uri;
                    msg.parts.push_back(std::move(img_part));
                } else if (!rf->content.text_content.empty()) {
                    core::ContentPart file_text;
                    file_text.type = "text";
                    file_text.text = "\n\n--- File: " + rf->name + " (" + rf->mime_type + ") ---\n"
                                    + rf->content.text_content
                                    + "\n--- End of file: " + rf->name + " ---\n";
                    msg.parts.push_back(std::move(file_text));
                }
            }
        } else if (!resolved_files.empty()) {
            std::string combined = user_message;
            for (auto* rf : resolved_files) {
                if (!rf->content.text_content.empty()) {
                    combined += "\n\n--- File: " + rf->name + " (" + rf->mime_type + ") ---\n"
                              + rf->content.text_content
                              + "\n--- End of file: " + rf->name + " ---\n";
                }
            }
            msg.role = "user";
            msg.content = combined;
        } else {
            msg.role = "user";
            msg.content = user_message;
        }

        sess->history.add_message("user", use_multipart ? user_message : msg.content);

        if (sess->options.auto_compact.value_or(true) && sess->history.needs_compaction()) {
            std::string cerr;
            compact_history_internal(sess, cerr);
        }

        auto req = build_request(sess, use_multipart ? user_message : msg.content);
        if (use_multipart && !msg.parts.empty()) {
            req.messages.back() = msg;
        }

        std::string full_response;
        execute_and_collect(req, callback, full_response);

        sess->history.add_message("assistant", full_response);
        sess->last_active = std::chrono::system_clock::now();

        if (storage_) {
            auto stored = session_to_stored(*sess);
            storage_->save_session(stored);
        }

        return true;
    }

}
