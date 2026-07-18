#define ZETLA_DLL_EXPORTS
#include "dll_api.h"
#include "../session/session_manager.hpp"
#include "../providers/registry.hpp"
#include "../agentic/agentic_loop.hpp"
#include "../core/config.hpp"
#include "../core/log.hpp"
#include "json_response.hpp"
#include "json_utils.hpp"
#include "nlohmann/json.hpp"
#include "../search/web_search_tool.hpp"
#include "../search/exa_provider.hpp"
#include <cstring>
#include <curl/curl.h>

using json = nlohmann::json;

static zetla::session::SessionManager* g_manager = nullptr;
static zetla_tool_executor_fn g_tool_executor = nullptr;

static char* to_cstr(const std::string& s) {
    char* buf = new char[s.size() + 1];
    std::memcpy(buf, s.c_str(), s.size() + 1);
    return buf;
}

static zetla_response make_response(const std::string& json_str) {
    zetla_response r;
    r.success = 1;
    r.data = to_cstr(json_str);
    r.error = nullptr;
    return r;
}

static zetla_response make_error(const std::string& code, const std::string& msg) {
    zetla_response r;
    r.success = 0;
    r.data = nullptr;
    r.error = to_cstr(zetla::json::err(code, msg));
    return r;
}

static std::string to_iso_time(std::chrono::system_clock::time_point tp) {
    auto tt = std::chrono::system_clock::to_time_t(tp);
    struct tm tm_buf;
#ifdef _WIN32
    gmtime_s(&tm_buf, &tt);
#else
    gmtime_r(&tt, &tm_buf);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return buf;
}

static zetla::core::ChatOptions parse_options_json(const std::string& json_str) {
    zetla::core::ChatOptions opts = zetla::core::ChatOptions::defaults();
    auto j = zetla::json::try_parse(json_str);
    if (!j) return opts;

    auto g = [&](const std::string& k) -> std::optional<nlohmann::json> {
        if (!j->contains(k)) return std::nullopt;
        return (*j)[k];
    };

    if (auto v = g("temperature")) opts.generation.temperature = v->get<float>();
    if (auto v = g("max_tokens")) opts.generation.max_tokens = v->get<int>();
    if (auto v = g("top_p")) opts.generation.top_p = v->get<float>();
    if (auto v = g("frequency_penalty")) opts.generation.frequency_penalty = v->get<float>();
    if (auto v = g("presence_penalty")) opts.generation.presence_penalty = v->get<float>();
    if (auto v = g("seed")) opts.generation.seed = v->get<int>();
    if (auto v = g("reasoning_effort")) opts.provider_options["reasoning_effort"] = v->get<std::string>();
    if (auto v = g("response_format")) opts.provider_options["response_format"] = v->get<std::string>();
    if (auto v = g("max_context_tokens")) opts.max_context_tokens = static_cast<size_t>(v->get<uint64_t>());
    if (auto v = g("auto_compact")) opts.auto_compact = v->get<bool>();
    if (auto v = g("compact_model")) opts.compact_model = v->get<std::string>();
    if (auto v = g("compact_on_save")) opts.compact_on_save = v->get<bool>();

    return opts;
}

ZETLA_API const char* zetla_version(void) {
    return "2.0.0";
}

static void ensure_curl_init() {
    zetla::network::HttpClient::global_init();
}

ZETLA_API int zetla_init(void) {
    if (g_manager) return 1;
    ensure_curl_init();
    g_manager = new zetla::session::SessionManager("~/.zetla/sessions");
    return 1;
}

ZETLA_API int zetla_init_ex(const char* storage_path) {
    if (g_manager) return 1;
    ensure_curl_init();
    std::string path = storage_path ? storage_path : "";
    g_manager = new zetla::session::SessionManager(path);
    return 1;
}

ZETLA_API void zetla_shutdown(void) {
    delete g_manager;
    g_manager = nullptr;
}

ZETLA_API void zetla_set_api_key(const char* api_key) {
    if (!api_key) return;
    auto& cfg = zetla::core::get_config();
    cfg.api_key = api_key;
    ZLOGI("set_api_key: key=%s", zetla::log::mask_key(api_key).c_str());
}

ZETLA_API void zetla_set_model(const char* model) {
    if (!model) return;
    ZLOGI("set_model: model=%s", model);
}

ZETLA_API zetla_response zetla_list_providers(void) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    std::string json_str = zetla::session::SessionManager::list_available_providers();
    return make_response(json_str);
}

ZETLA_API zetla_response zetla_set_provider(const char* provider_id) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    if (!provider_id) return make_error("INVALID_ARG", "provider_id is null");

    auto& cfg = zetla::core::get_config();
    std::string pid(provider_id);

    auto it = cfg.providers.find(pid);
    std::string api_key = (it != cfg.providers.end()) ? it->second.api_key : cfg.api_key;
    std::string base_url = (it != cfg.providers.end()) ? it->second.base_url : "";

    ZLOGI("set_provider: id=%s api_key=%s base_url=%s",
        pid.c_str(), zetla::log::mask_key(api_key).c_str(), base_url.c_str());

    zetla::providers::ProviderConfig config;
    config.id = pid;
    config.api_key = api_key;
    config.base_url = base_url;
    config.enabled = true;

    auto provider = zetla::providers::create_provider(config);
    if (!provider) {
        return make_error("UNKNOWN_PROVIDER", "Unknown provider: " + pid);
    }

    g_manager->set_provider(std::move(provider));
    json j;
    j["provider"] = pid;
    j["status"] = "active";
    return make_response(j.dump());
}

ZETLA_API zetla_response zetla_list_models(void) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    std::string json_str = g_manager->list_models();
    return make_response(json_str);
}

ZETLA_API void zetla_set_default_options(const char* options_json) {
    if (!options_json) return;
    auto& cfg = zetla::core::get_config();
    cfg.default_options = parse_options_json(options_json);
}

ZETLA_API zetla_response zetla_get_default_options(void) {
    auto& cfg = zetla::core::get_config();
    auto& opts = cfg.default_options;
    json j;
    if (opts.generation.temperature.has_value()) j["temperature"] = opts.generation.temperature.value();
    if (opts.generation.max_tokens.has_value()) j["max_tokens"] = opts.generation.max_tokens.value();
    if (opts.generation.top_p.has_value()) j["top_p"] = opts.generation.top_p.value();
    if (opts.generation.frequency_penalty.has_value()) j["frequency_penalty"] = opts.generation.frequency_penalty.value();
    if (opts.generation.presence_penalty.has_value()) j["presence_penalty"] = opts.generation.presence_penalty.value();
    if (opts.generation.seed.has_value()) j["seed"] = opts.generation.seed.value();
    for (auto& [k, v] : opts.provider_options) j[k] = v;
    if (opts.max_context_tokens.has_value()) j["max_context_tokens"] = static_cast<int>(opts.max_context_tokens.value());
    return make_response(j.dump());
}

    ZETLA_API void zetla_cancel_request(void) {
        zetla::network::HttpClient::request_cancel();
    }

    ZETLA_API void zetla_set_system_prompt(const char* system_prompt) {
    if (!system_prompt) return;
    auto& cfg = zetla::core::get_config();
    cfg.system_prompt = system_prompt;
}

ZETLA_API zetla_response zetla_set_session_system_prompt(const char* session_id, const char* system_prompt) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    if (!session_id || !system_prompt) return make_error("INVALID_ARG", "session_id or system_prompt is null");
    std::string sid(session_id);
    std::string sp(system_prompt);
    bool ok = g_manager->set_session_system_prompt(sid, sp);
    if (!ok) return make_error("SESSION_NOT_FOUND", "Session '" + sid + "' not found");
    nlohmann::json j;
    j["session_id"] = sid;
    j["status"] = "system_prompt_updated";
    return make_response(j.dump());
}

ZETLA_API zetla_response zetla_get_system_prompt(void) {
    auto& cfg = zetla::core::get_config();
    json j;
    j["system_prompt"] = cfg.system_prompt;
    return make_response(j.dump());
}

ZETLA_API void zetla_set_provider_config(const char* provider_id, const char* api_key, const char* base_url, int enabled) {
    if (!provider_id) return;
    auto& cfg = zetla::core::get_config();
    std::string pid(provider_id);
    auto& pc = cfg.providers[pid];
    if (api_key) pc.api_key = api_key;
    if (base_url) pc.base_url = base_url;
    pc.enabled = (enabled != 0);
    ZLOGI("set_provider_config: id=%s api_key=%s base_url=%s enabled=%d",
        pid.c_str(),
        api_key ? zetla::log::mask_key(api_key).c_str() : "null",
        base_url ? base_url : "null",
        enabled);
}

ZETLA_API zetla_response zetla_get_provider_config(const char* provider_id) {
    if (!provider_id) return make_error("INVALID_ARG", "provider_id is null");
    auto& cfg = zetla::core::get_config();
    std::string pid(provider_id);
    auto it = cfg.providers.find(pid);
    json j;
    j["provider_id"] = pid;
    if (it == cfg.providers.end()) {
        j["api_key"] = "";
        j["base_url"] = "";
        j["enabled"] = false;
    } else {
        j["api_key"] = it->second.api_key;
        j["base_url"] = it->second.base_url;
        j["enabled"] = it->second.enabled;
    }
    return make_response(j.dump());
}

ZETLA_API zetla_response zetla_list_provider_configs(void) {
    auto& cfg = zetla::core::get_config();
    json arr = json::array();
    for (auto& [pid, pc] : cfg.providers) {
        arr.push_back({{"provider_id", pid}, {"api_key", pc.api_key}, {"base_url", pc.base_url}, {"enabled", pc.enabled}});
    }
    return make_response(arr.dump());
}

ZETLA_API zetla_response zetla_list_providers_models(void) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    auto& cfg = zetla::core::get_config();
    json arr = json::array();

    for (auto& [pid, pc] : cfg.providers) {
        if (!pc.enabled || pc.api_key.empty()) continue;

        zetla::providers::ProviderConfig config;
        config.id = pid;
        config.api_key = pc.api_key;
        config.base_url = pc.base_url;
        config.enabled = true;

        auto provider = zetla::providers::create_provider(config);
        if (provider) {
            std::string models = provider->list_models();
            if (models.size() > 2) {
                auto models_arr = json::parse(models);
                if (models_arr.is_array()) {
                    for (auto& m : models_arr) {
                        arr.push_back(m);
                    }
                }
            }
        }
    }

    return make_response(arr.dump());
}

ZETLA_API int zetla_set_session_web_search(const char* session_id, int enabled) {
    if (!g_manager) return 0;
    if (!session_id) return 0;

    std::string sid(session_id);

    if (enabled) {
        auto& cfg = zetla::core::get_config();
        // Always use EXA MCP (no DuckDuckGo — doesn't work on Android)
        std::unique_ptr<zetla::search::ISearchProvider> search_provider =
            std::make_unique<zetla::search::ExaSearchProvider>(
                cfg.exa_api_key,
                cfg.exa_mcp_url.empty()
                    ? "https://mcp.exa.ai/mcp"
                    : cfg.exa_mcp_url
            );

        auto tool = std::make_unique<zetla::search::WebSearchTool>(
            std::move(search_provider), 5
        );
        g_manager->add_tool_to_session(sid, std::move(tool));
    }

    return 1;
}

ZETLA_API void zetla_set_search_provider(const char* provider) {
    if (!provider) return;
    auto& cfg = zetla::core::get_config();
    std::string p(provider);
    if (p == "exa") {
        cfg.search_provider = p;
    }
}

ZETLA_API void zetla_set_exa_api_key(const char* api_key) {
    if (!api_key) return;
    auto& cfg = zetla::core::get_config();
    cfg.exa_api_key = api_key;
}

ZETLA_API zetla_response zetla_create_session(const char* model, const char* system_prompt) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");

    std::string m = model ? model : "deepseek-v4-flash";
    std::string sp = system_prompt ? system_prompt : "";
    ZLOGI("create_session: model=%s system_prompt=%s", m.c_str(), zetla::log::truncate(sp, 200).c_str());
    std::string id = g_manager->create_session(m, sp);

    return make_response(zetla::json::session_created(id, m));
}

ZETLA_API zetla_response zetla_delete_session(const char* session_id) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    if (!session_id) return make_error("INVALID_ARG", "session_id is null");

    std::string sid(session_id);
    bool ok = g_manager->delete_session(sid);
    if (!ok) return make_error("SESSION_NOT_FOUND", "Session '" + sid + "' not found");

    return make_response(zetla::json::session_deleted(sid));
}

ZETLA_API zetla_response zetla_get_session_info(const char* session_id) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    if (!session_id) return make_error("INVALID_ARG", "session_id is null");

    std::string sid(session_id);
    auto info = g_manager->get_session_info(sid);
    if (info.model.empty()) return make_error("SESSION_NOT_FOUND", "Session '" + sid + "' not found");

    return make_response(zetla::json::session_info(
        sid, info.model, info.message_count,
        to_iso_time(info.created_at), to_iso_time(info.last_active)
    ));
}

ZETLA_API int zetla_send_message(const char* session_id, const char* message, zetla_token_fn callback) {
    if (!g_manager || !callback) return 0;
    if (!session_id || !message) return 0;

    zetla::network::HttpClient::request_reset();

    std::string sid(session_id);
    std::string err;
    ZLOGI("send_message: session=%s message=%s", sid.c_str(), zetla::log::truncate(message, 200).c_str());

    // Set tool executor on session if available (for Java-side tool callbacks like Python tool)
    if (g_tool_executor) {
        zetla::core::ToolExecutorCallback executor =
            [sid, cb = g_tool_executor](const zetla::core::ToolCallRequest& tc) -> zetla::core::ToolCallResult {
                char* result_cstr = cb(sid.c_str(), tc.name.c_str(), tc.arguments_json.c_str());
                zetla::core::ToolCallResult tr;
                tr.tool_call_id = tc.id;
                if (result_cstr) {
                    tr.content = result_cstr;
                } else {
                    nlohmann::json j;
                    j["error"] = "Tool executor returned null";
                    tr.content = j.dump();
                    tr.is_error = true;
                }
                return tr;
            };
        g_manager->set_tool_executor(sid, executor);
    }

    bool ok = g_manager->send_message(sid, message,
        [sid, callback](const zetla::core::StreamChunk& chunk) {
            std::string json_str = zetla::json::chunk(sid, chunk.delta_content, chunk.reasoning, chunk.is_finished);
            callback(json_str.c_str(), chunk.is_finished ? 1 : 0);
        }, err);
    if (!ok) ZLOGE("send_message: FAILED session=%s error=%s", sid.c_str(), err.c_str());
    return ok ? 1 : 0;
}

ZETLA_API int zetla_send_message_sse(const char* session_id, const char* message, zetla_sse_fn callback) {
    if (!g_manager || !callback) return 0;
    if (!session_id || !message) return 0;

    zetla::network::HttpClient::request_reset();

    std::string sid(session_id);
    std::string err;
    bool ok = g_manager->send_message_sse(sid, message,
        [callback](const std::string& json_data, bool is_done) {
            callback(json_data.c_str(), is_done ? 1 : 0);
        }, err);
    return ok ? 1 : 0;
}

ZETLA_API zetla_response zetla_set_session_options(const char* session_id, const char* options_json) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    if (!session_id) return make_error("INVALID_ARG", "session_id is null");
    if (!options_json) return make_error("INVALID_ARG", "options_json is null");

    std::string sid(session_id);
    std::string json_str(options_json);

    auto title_val = zetla::json::extract_string(json_str, "title");
    if (!title_val.empty()) {
        g_manager->set_session_title(sid, title_val);
    }

    auto starred_val = zetla::json::extract_string(json_str, "is_starred");
    if (!starred_val.empty()) {
        bool starred = (starred_val == "true" || starred_val == "1");
        g_manager->set_session_starred(sid, starred);
    }

    auto opts = parse_options_json(json_str);
    bool ok = g_manager->set_session_options(sid, opts);
    if (!ok) return make_error("SESSION_NOT_FOUND", "Session '" + sid + "' not found");

    json j;
    j["session_id"] = sid;
    j["status"] = "options_updated";
    return make_response(j.dump());
}

ZETLA_API zetla_response zetla_get_session_options(const char* session_id) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    if (!session_id) return make_error("INVALID_ARG", "session_id is null");

    std::string sid(session_id);
    auto opts = g_manager->get_session_options(sid);

    json j;
    j["session_id"] = sid;
    if (opts.generation.temperature.has_value()) j["temperature"] = opts.generation.temperature.value();
    if (opts.generation.max_tokens.has_value()) j["max_tokens"] = opts.generation.max_tokens.value();
    if (opts.generation.top_p.has_value()) j["top_p"] = opts.generation.top_p.value();
    if (opts.generation.frequency_penalty.has_value()) j["frequency_penalty"] = opts.generation.frequency_penalty.value();
    if (opts.generation.presence_penalty.has_value()) j["presence_penalty"] = opts.generation.presence_penalty.value();
    if (opts.generation.seed.has_value()) j["seed"] = opts.generation.seed.value();
    for (auto& [k, v] : opts.provider_options) j[k] = v;

    return make_response(j.dump());
}

ZETLA_API zetla_response zetla_get_history(const char* session_id) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    if (!session_id) return make_error("INVALID_ARG", "session_id is null");

    std::string sid(session_id);
    auto msgs = g_manager->get_history(sid);
    if (msgs.empty()) {
        auto info = g_manager->get_session_info(sid);
        if (info.model.empty()) return make_error("SESSION_NOT_FOUND", "Session '" + sid + "' not found");
    }

    json arr = json::array();
    for (auto& m : msgs) {
        arr.push_back({{"role", m.role}, {"content", m.content}});
    }

    return make_response(zetla::json::history(sid, arr.dump()));
}

ZETLA_API zetla_response zetla_clear_history(const char* session_id) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    if (!session_id) return make_error("INVALID_ARG", "session_id is null");

    std::string sid(session_id);
    bool ok = g_manager->clear_history(sid);
    if (!ok) return make_error("SESSION_NOT_FOUND", "Session '" + sid + "' not found");

    return make_response(zetla::json::history_cleared(sid));
}

ZETLA_API zetla_response zetla_set_session_model(const char* session_id, const char* model) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    if (!session_id || !model) return make_error("INVALID_ARG", "session_id or model is null");
    std::string sid(session_id);
    std::string m(model);
    bool ok = g_manager->set_session_model(sid, m);
    if (!ok) return make_error("SESSION_NOT_FOUND", "Session '" + sid + "' not found");
    json j;
    j["session_id"] = sid;
    j["model"] = m;
    return make_response(j.dump());
}

ZETLA_API zetla_response zetla_load_session(const char* session_id) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    if (!session_id) return make_error("INVALID_ARG", "session_id is null");

    std::string sid(session_id);
    bool ok = g_manager->load_from_storage(sid);
    if (!ok) return make_error("LOAD_FAILED", "Could not load session '" + sid + "' from storage");

    auto info = g_manager->get_session_info(sid);
    return make_response(zetla::json::session_info(
        sid, info.model, info.message_count,
        to_iso_time(info.created_at), to_iso_time(info.last_active)
    ));
}

ZETLA_API zetla_response zetla_list_sessions(void) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");

    auto sessions = g_manager->list_persisted_sessions_full();
    json arr = json::array();
    for (auto& s : sessions) {
        arr.push_back({
            {"session_id", s.id},
            {"model", s.model},
            {"title", s.title},
            {"is_starred", s.is_starred},
            {"created_at", s.created_at},
            {"last_active", s.last_active},
            {"has_compacted_summary", !s.compacted_summary.empty()}
        });
    }

    return make_response(zetla::json::ok(arr.dump()));
}

ZETLA_API zetla_response zetla_delete_from_storage(const char* session_id) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    if (!session_id) return make_error("INVALID_ARG", "session_id is null");

    std::string sid(session_id);
    bool ok = g_manager->delete_from_storage(sid);
    if (!ok) return make_error("DELETE_FAILED", "Could not delete session '" + sid + "' from storage");

    json j;
    j["session_id"] = sid;
    j["status"] = "deleted_from_storage";
    return make_response(j.dump());
}

ZETLA_API zetla_response zetla_session_exists_on_disk(const char* session_id) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    if (!session_id) return make_error("INVALID_ARG", "session_id is null");

    std::string sid(session_id);
    bool exists = g_manager->session_exists_on_disk(sid);
    json j;
    j["session_id"] = sid;
    j["exists"] = exists;
    return make_response(j.dump());
}

ZETLA_API zetla_response zetla_compact_session(const char* session_id) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    if (!session_id) return make_error("INVALID_ARG", "session_id is null");

    std::string sid(session_id);
    std::string err;
    bool ok = g_manager->compact_session(sid, err);
    if (!ok) return make_error("COMPACTION_FAILED", err);

    json j;
    j["session_id"] = sid;
    j["status"] = "compacted";
    return make_response(j.dump());
}

ZETLA_API zetla_response zetla_get_compaction_info(const char* session_id) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    if (!session_id) return make_error("INVALID_ARG", "session_id is null");

    std::string sid(session_id);
    auto info = g_manager->get_compaction_info(sid);
    if (info.session_id.empty()) return make_error("SESSION_NOT_FOUND", "Session '" + sid + "' not found");

    json j;
    j["session_id"] = sid;
    j["estimated_tokens"] = info.estimated_tokens;
    j["max_context_tokens"] = info.max_context_tokens;
    j["needs_compaction"] = info.needs_compaction;
    j["compacted_summary"] = info.compacted_summary;
    j["history_turns"] = info.history_turns;
    j["total_messages"] = info.total_messages;

    return make_response(j.dump());
}

ZETLA_API zetla_response zetla_add_tool(
    const char* session_id,
    const char* name,
    const char* description,
    const char* parameters_schema_json
) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    if (!session_id) return make_error("INVALID_ARG", "session_id is null");
    if (!name) return make_error("INVALID_ARG", "name is null");

    std::string sid(session_id);
    std::string n = name;
    std::string d = description ? description : "";
    std::string schema_json = parameters_schema_json ? parameters_schema_json : "{}";

    zetla::core::ToolDefinition def;
    def.name = n;
    def.description = d;
    def.parameters_schema_json = schema_json;

    bool ok = g_manager->add_tool_definition(sid, def);
    if (!ok) return make_error("SESSION_NOT_FOUND", "Session '" + sid + "' not found");

    json j;
    j["session_id"] = sid;
    j["tool"] = n;
    j["status"] = "added";
    return make_response(j.dump());
}

ZETLA_API void zetla_set_tool_executor(zetla_tool_executor_fn fn) {
    g_tool_executor = fn;
}

ZETLA_API int zetla_send_message_with_images(
    const char* session_id,
    const char* message,
    const char** image_data_uris,
    int image_count,
    zetla_token_fn callback
) {
    if (!g_manager || !callback) return 0;
    if (!session_id || !message) return 0;

    zetla::network::HttpClient::request_reset();

    std::string sid(session_id);
    std::string err;

    zetla::core::Message msg;
    msg.role = "user";

    if (image_count > 0 && image_data_uris) {
        zetla::core::ContentPart text_part;
        text_part.type = "text";
        text_part.text = message;
        msg.parts.push_back(std::move(text_part));

        for (int i = 0; i < image_count; ++i) {
            if (!image_data_uris[i]) continue;
            zetla::core::ContentPart img_part;
            img_part.type = "image_url";
            img_part.image_url = image_data_uris[i];
            msg.parts.push_back(std::move(img_part));
        }
    } else {
        msg.content = message;
    }

    bool ok = g_manager->send_message_multipart(sid, msg,
        [sid, callback](const zetla::core::StreamChunk& chunk) {
            std::string json_str = zetla::json::chunk(sid, chunk.delta_content, chunk.reasoning, chunk.is_finished);
            callback(json_str.c_str(), chunk.is_finished ? 1 : 0);
        }, err);
    return ok ? 1 : 0;
}

ZETLA_API int zetla_send_message_with_images_sse(
    const char* session_id,
    const char* message,
    const char** image_data_uris,
    int image_count,
    zetla_sse_fn callback
) {
    if (!g_manager || !callback) return 0;
    if (!session_id || !message) return 0;

    zetla::network::HttpClient::request_reset();

    std::string sid(session_id);
    std::string err;

    zetla::core::Message msg;
    msg.role = "user";

    if (image_count > 0 && image_data_uris) {
        zetla::core::ContentPart text_part;
        text_part.type = "text";
        text_part.text = message;
        msg.parts.push_back(std::move(text_part));

        for (int i = 0; i < image_count; ++i) {
            if (!image_data_uris[i]) continue;
            zetla::core::ContentPart img_part;
            img_part.type = "image_url";
            img_part.image_url = image_data_uris[i];
            msg.parts.push_back(std::move(img_part));
        }
    } else {
        msg.content = message;
    }

    bool ok = g_manager->send_message_multipart_sse(sid, msg,
        [callback](const std::string& json_data, bool is_done) {
            callback(json_data.c_str(), is_done ? 1 : 0);
        }, err);
    return ok ? 1 : 0;
}

ZETLA_API int zetla_send_message_agentic(
    const char* session_id,
    const char* message,
    zetla_agentic_fn callback
) {
    if (!g_manager || !callback) return 0;
    if (!session_id || !message) return 0;

    zetla::network::HttpClient::request_reset();

    std::string sid(session_id);
    std::string err;

    zetla::core::ToolExecutorCallback executor;
    if (g_tool_executor) {
        executor = [sid, cb = g_tool_executor](const zetla::core::ToolCallRequest& tc) -> zetla::core::ToolCallResult {
            char* result_cstr = cb(sid.c_str(), tc.name.c_str(), tc.arguments_json.c_str());
            zetla::core::ToolCallResult tr;
            tr.tool_call_id = tc.id;
            if (result_cstr) {
                tr.content = result_cstr;
            } else {
                json j;
                j["error"] = "Tool executor returned null";
                tr.content = j.dump();
                tr.is_error = true;
            }
            return tr;
        };
    }

    bool ok = g_manager->set_tool_executor(sid, executor);
    if (!ok) return 0;

    ok = g_manager->send_message_agentic(sid, message,
        [sid, callback](const zetla::core::AgenticEvent& event) {
            json j;
            j["session_id"] = sid;
            switch (event.type) {
                case zetla::core::AgenticEvent::THINKING: j["type"] = "thinking"; break;
                case zetla::core::AgenticEvent::TOOL_CALL: j["type"] = "tool_call"; break;
                case zetla::core::AgenticEvent::TOOL_RESULT: j["type"] = "tool_result"; break;
                case zetla::core::AgenticEvent::CONTENT: j["type"] = "content"; break;
                case zetla::core::AgenticEvent::DONE: j["type"] = "done"; break;
            }
            if (!event.data.empty()) j["data"] = event.data;
            if (!event.tool_name.empty()) j["tool_name"] = event.tool_name;
            if (!event.tool_call_id.empty()) j["tool_id"] = event.tool_call_id;
            j["finished"] = event.is_finished;
            std::string json_str = j.dump();
            callback(json_str.c_str());
        }, err);

    return ok ? 1 : 0;
}

ZETLA_API zetla_response zetla_add_file(
    const char* session_id,
    const char* file_path
) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    if (!session_id || !file_path) return make_error("INVALID_ARG", "session_id or file_path is null");

    std::string sid(session_id);
    std::string fp(file_path);
    std::string err;

    std::string file_id = g_manager->add_file(sid, fp, err);
    if (file_id.empty()) return make_error("FILE_FAILED", err);

    auto files = g_manager->list_files(sid);
    std::string name, mime;
    size_t sz = 0;
    for (auto& f : files) {
        if (f.file_id == file_id) {
            name = f.name;
            mime = f.mime_type;
            sz = f.size_bytes;
            break;
        }
    }

    json j;
    j["file_id"] = file_id;
    j["name"] = name;
    j["mime_type"] = mime;
    j["size"] = sz;
    return make_response(j.dump());
}

ZETLA_API zetla_response zetla_remove_file(
    const char* session_id,
    const char* file_id
) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    if (!session_id || !file_id) return make_error("INVALID_ARG", "session_id or file_id is null");

    std::string sid(session_id);
    std::string fid(file_id);
    bool ok = g_manager->remove_file(sid, fid);
    if (!ok) return make_error("FILE_NOT_FOUND", "File '" + fid + "' not found in session");

    json j;
    j["file_id"] = fid;
    j["status"] = "removed";
    return make_response(j.dump());
}

ZETLA_API zetla_response zetla_list_files(const char* session_id) {
    if (!g_manager) return make_error("NOT_INITIALIZED", "Call zetla_init() first");
    if (!session_id) return make_error("INVALID_ARG", "session_id is null");

    std::string sid(session_id);
    auto files = g_manager->list_files(sid);

    json files_arr = json::array();
    for (auto& f : files) {
        files_arr.push_back({
            {"file_id", f.file_id},
            {"name", f.name},
            {"mime_type", f.mime_type},
            {"size", f.size_bytes},
            {"type", zetla::file_handlers::content_type_name(f.content.type)}
        });
    }

    json j;
    j["files"] = files_arr;
    return make_response(j.dump());
}

ZETLA_API int zetla_send_message_with_files(
    const char* session_id,
    const char* message,
    const char** file_ids,
    int file_count,
    zetla_token_fn callback
) {
    if (!g_manager || !callback) return 0;
    if (!session_id || !message) return 0;

    zetla::network::HttpClient::request_reset();

    std::string sid(session_id);
    std::string err;
    std::vector<std::string> fids;
    for (int i = 0; i < file_count; ++i) {
        if (file_ids[i]) fids.push_back(file_ids[i]);
    }

    bool ok = g_manager->send_message_with_files(sid, message, fids,
        [sid, callback](const zetla::core::StreamChunk& chunk) {
            std::string json_str = zetla::json::chunk(sid, chunk.delta_content, chunk.reasoning, chunk.is_finished);
            callback(json_str.c_str(), chunk.is_finished ? 1 : 0);
        }, err);
    return ok ? 1 : 0;
}

ZETLA_API void zetla_free_response(zetla_response* response) {
    if (!response) return;
    delete[] response->data;
    delete[] response->error;
    response->data = nullptr;
    response->error = nullptr;
}

ZETLA_API void zetla_free_string(char* str) {
    delete[] str;
}
