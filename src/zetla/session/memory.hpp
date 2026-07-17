#pragma once
#include "../core/types.hpp"
#include <mutex>
#include <vector>
#include <string>

namespace zetla::memory {

    class ChatHistory {
    private:
        std::string system_prompt_;
        std::vector<core::Message> history_;
        mutable std::mutex history_mutex_;
        std::string compacted_summary_;
        size_t max_context_tokens_ = 8192;

        static size_t estimate_tokens(const std::string& text) {
            return (text.size() + 3) / 4;
        }

        size_t estimate_payload_tokens() const {
            size_t tokens = estimate_tokens(system_prompt_);
            if (!compacted_summary_.empty()) {
                tokens += 5;
                tokens += estimate_tokens(compacted_summary_);
            }
            for (auto& m : history_) {
                tokens += 4;
                tokens += estimate_tokens(m.content);
            }
            return tokens;
        }

    public:
        explicit ChatHistory(std::string system_instruction, size_t max_context_tokens = 8192)
            : system_prompt_(std::move(system_instruction))
            , max_context_tokens_(max_context_tokens) {}

        void add_message(const std::string& role, const std::string& content) {
            std::lock_guard<std::mutex> lock(history_mutex_);
            history_.push_back({role, content});
        }

        std::vector<core::Message> build_payload() {
            std::lock_guard<std::mutex> lock(history_mutex_);
            std::vector<core::Message> combined;

            if (!compacted_summary_.empty()) {
                combined.push_back({"system",
                    "[Previous conversation summary]\n" + compacted_summary_
                    + "\n[/Previous conversation summary]\n\n"
                    + "Continue the conversation based on the summary above."});
            }

            combined.insert(combined.end(), history_.begin(), history_.end());
            return combined;
        }

        std::string system_prompt() const {
            std::lock_guard<std::mutex> lock(history_mutex_);
            return system_prompt_;
        }

        void set_system_prompt(const std::string& sp) {
            std::lock_guard<std::mutex> lock(history_mutex_);
            system_prompt_ = sp;
        }

        void clear() {
            std::lock_guard<std::mutex> lock(history_mutex_);
            history_.clear();
            compacted_summary_.clear();
        }

        std::vector<core::Message> history_snapshot() const {
            std::lock_guard<std::mutex> lock(history_mutex_);
            return history_;
        }

        size_t estimated_tokens() const {
            std::lock_guard<std::mutex> lock(history_mutex_);
            return estimate_payload_tokens();
        }

        bool needs_compaction() const {
            std::lock_guard<std::mutex> lock(history_mutex_);
            return !compacted_summary_.empty() ?
                estimate_payload_tokens() > (max_context_tokens_ * 19 / 20) :
                estimate_payload_tokens() > (max_context_tokens_ * 2 / 3);
        }

        size_t max_context_tokens() const {
            std::lock_guard<std::mutex> lock(history_mutex_);
            return max_context_tokens_;
        }

        void set_max_context_tokens(size_t max) {
            std::lock_guard<std::mutex> lock(history_mutex_);
            max_context_tokens_ = max;
        }

        std::string compacted_summary() const {
            std::lock_guard<std::mutex> lock(history_mutex_);
            return compacted_summary_;
        }

        void set_compacted_summary(const std::string& summary) {
            std::lock_guard<std::mutex> lock(history_mutex_);
            compacted_summary_ = summary;
        }

        std::vector<core::Message> messages_to_compact() const {
            std::lock_guard<std::mutex> lock(history_mutex_);
            if (history_.size() <= 4) return {};
            return std::vector<core::Message>(
                history_.begin(), history_.end() - 4);
        }

        std::vector<core::Message> messages_to_keep() const {
            std::lock_guard<std::mutex> lock(history_mutex_);
            if (history_.size() <= 4) return history_;
            return std::vector<core::Message>(
                history_.end() - 4, history_.end());
        }

        void apply_compaction(const std::string& new_summary,
                              const std::vector<core::Message>& keep) {
            std::lock_guard<std::mutex> lock(history_mutex_);
            compacted_summary_ = new_summary;
            history_ = keep;
        }

        size_t message_count() const {
            std::lock_guard<std::mutex> lock(history_mutex_);
            return history_.size();
        }

        bool has_compacted_summary() const {
            std::lock_guard<std::mutex> lock(history_mutex_);
            return !compacted_summary_.empty();
        }
    };

}
