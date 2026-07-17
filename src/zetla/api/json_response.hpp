#pragma once
#include "json_utils.hpp"
#include "nlohmann/json.hpp"
#include <string>

namespace zetla::json {

    inline std::string ok(const std::string& data_json) {
        nlohmann::json j;
        j["success"] = true;
        j["data"] = nlohmann::json::parse(data_json);
        j["error"] = nullptr;
        return j.dump();
    }

    inline std::string err(const std::string& code, const std::string& message) {
        nlohmann::json j;
        j["success"] = false;
        j["data"] = nullptr;
        j["error"] = {{"code", code}, {"message", message}};
        return j.dump();
    }

    inline std::string session_created(const std::string& session_id, const std::string& model) {
        nlohmann::json j;
        j["session_id"] = session_id;
        j["model"] = model;
        j["status"] = "created";
        return ok(j.dump());
    }

    inline std::string session_deleted(const std::string& session_id) {
        nlohmann::json j;
        j["session_id"] = session_id;
        j["status"] = "deleted";
        return ok(j.dump());
    }

    inline std::string session_info(
        const std::string& session_id,
        const std::string& model,
        size_t message_count,
        const std::string& created_at,
        const std::string& last_active
    ) {
        nlohmann::json j;
        j["session_id"] = session_id;
        j["model"] = model;
        j["message_count"] = message_count;
        j["created_at"] = created_at;
        j["last_active"] = last_active;
        return ok(j.dump());
    }

    inline std::string history(const std::string& session_id, const std::string& messages_json) {
        nlohmann::json j;
        j["session_id"] = session_id;
        j["messages"] = messages_json;
        return ok(j.dump());
    }

    inline std::string history_cleared(const std::string& session_id) {
        nlohmann::json j;
        j["session_id"] = session_id;
        j["status"] = "cleared";
        return ok(j.dump());
    }

    inline std::string chunk(
        const std::string& session_id,
        const std::string& delta,
        const std::string& reasoning,
        bool finished
    ) {
        nlohmann::json j;
        j["session_id"] = session_id;
        j["delta"] = delta;
        if (!reasoning.empty()) j["reasoning"] = reasoning;
        j["finished"] = finished;
        return j.dump();
    }

}
