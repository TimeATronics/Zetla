#include "serialization.hpp"
#include "nlohmann/json.hpp"
#include <zlib.h>

namespace zetla::storage {

    bool Serializer::serialize(const StoredSession& session,
                               std::vector<uint8_t>& out,
                               int compression_level)
    {
        nlohmann::json j;
        j["id"] = session.id;
        j["model"] = session.model;
        j["title"] = session.title;
        j["is_starred"] = session.is_starred;
        j["is_space"] = session.is_space;
        j["space_files"] = session.space_files_json.empty() ? nlohmann::json::array() : nlohmann::json::parse(session.space_files_json);
        j["system_prompt"] = session.system_prompt;

        nlohmann::json msgs = nlohmann::json::array();
        for (auto& [role, content] : session.messages) {
            msgs.push_back({{"role", role}, {"content", content}});
        }
        j["messages"] = msgs;

        j["options"] = session.options_json.empty() ? nlohmann::json::object() : nlohmann::json::parse(session.options_json);
        j["compacted_summary"] = session.compacted_summary;
        j["created_at_ms"] = session.created_at_ms;
        j["last_active_ms"] = session.last_active_ms;

        std::string json_str = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);

        uLongf dest_len = compressBound(static_cast<uLong>(json_str.size()));
        out.resize(dest_len);
        int ret = compress2(out.data(), &dest_len,
                            reinterpret_cast<const uint8_t*>(json_str.data()),
                            static_cast<uLong>(json_str.size()), compression_level);
        if (ret != Z_OK) return false;
        out.resize(dest_len);
        return true;
    }

    bool Serializer::deserialize(const uint8_t* data, size_t len,
                                 StoredSession& out)
    {
        uLongf dest_len = len * 4;
        std::vector<uint8_t> json_bytes;
        int ret;
        do {
            json_bytes.resize(dest_len);
            ret = uncompress(json_bytes.data(), &dest_len, data, static_cast<uLong>(len));
            if (ret == Z_BUF_ERROR) dest_len *= 2;
        } while (ret == Z_BUF_ERROR);
        if (ret != Z_OK) return false;
        json_bytes.resize(dest_len);

        std::string json_str(json_bytes.begin(), json_bytes.end());

        try {
            auto j = nlohmann::json::parse(json_str);

            out.id = j.value("id", "");
            out.model = j.value("model", "");
            out.title = j.value("title", "");
            out.is_starred = j.value("is_starred", false);
            out.is_space = j.value("is_space", false);
            if (j.contains("space_files")) {
                out.space_files_json = j["space_files"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            }
            out.system_prompt = j.value("system_prompt", "");
            out.compacted_summary = j.value("compacted_summary", "");
            out.created_at_ms = j.value("created_at_ms", 0);
            out.last_active_ms = j.value("last_active_ms", 0);

            if (j.contains("options")) {
                out.options_json = j["options"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            } else {
                out.options_json = "";
            }

            out.messages.clear();
            if (j.contains("messages") && j["messages"].is_array()) {
                for (auto& m : j["messages"]) {
                    out.messages.push_back({m.value("role", ""), m.value("content", "")});
                }
            }
        } catch (...) {
            return false;
        }

        return !out.id.empty();
    }

}
