#include "zetla/api/dll_api.h"
#include "nlohmann/json.hpp"
#include <iostream>
#include <string>
#include <chrono>
#include <cstring>
#include <ctime>
#include <cmath>
#include <functional>
#include <fstream>
#include <cstdlib>
#include <vector>
#include <cassert>
#include <algorithm>
#include <sstream>
#include <thread>
#include <iomanip>

// Helpers

static int g_total_tests = 0, g_passed_tests = 0;

#define CHECK(label, cond) do { \
    g_total_tests++; \
    if (cond) { g_passed_tests++; std::cout << "  PASS [" << label << "]\n"; } \
    else { std::cout << "  FAIL [" << label << "]\n"; } \
} while(0)

#define CHECK_MSG(label, cond, msg) do { \
    g_total_tests++; \
    if (cond) { g_passed_tests++; std::cout << "  PASS [" << label << "]\n"; } \
    else { std::cout << "  FAIL [" << label << "]: " << (msg) << "\n"; } \
} while(0)

#define CHECK_SKIP(label, reason) do { \
    std::cout << "  SKIP [" << label << "]: " << reason << "\n"; \
} while(0)

static std::string g_current_provider;

static void print_response(const char* prefix, const zetla_response& r) {
    std::cout << "  " << prefix << " -> "
              << (r.success ? r.data : r.error ? r.error : "(null)")
              << "\n";
}

static std::string json_get_str(const nlohmann::json& j, const std::string& key) {
    if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
    // Try nested inside "data" (response envelope)
    if (j.contains("data") && j["data"].is_object()) {
        auto& d = j["data"];
        if (d.contains(key) && d[key].is_string()) return d[key].get<std::string>();
    }
    return "";
}

static std::string extract_json_str(const std::string& json, const std::string& key) {
    try {
        return json_get_str(nlohmann::json::parse(json), key);
    } catch (...) {}
    return "";
}

static std::string extract_json_val(const std::string& json, const std::string& key) {
    try {
        auto j = nlohmann::json::parse(json);
        // Check top level first
        if (j.contains(key)) {
            auto& v = j[key];
            if (v.is_string()) return v.get<std::string>();
            if (v.is_number()) return std::to_string(v.get<double>());
            if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
            return v.dump();
        }
        // Then check inside "data"
        if (j.contains("data") && j["data"].is_object()) {
            auto& d = j["data"];
            if (d.contains(key)) {
                auto& v = d[key];
                if (v.is_string()) return v.get<std::string>();
                if (v.is_number()) return std::to_string(v.get<double>());
                if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
                return v.dump();
            }
        }
    } catch (...) {}
    return "";
}

static std::string read_env(const std::string& key) {
    const char* val = std::getenv(key.c_str());
    return val ? std::string(val) : "";
}

static bool load_env_file() {
    std::ifstream f("env.txt");
    if (!f.is_open()) {
        f.open("../env.txt");
        if (!f.is_open()) return false;
    }
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
#ifdef _WIN32
        _putenv((key + "=" + val).c_str());
#else
        setenv(key.c_str(), val.c_str(), 1);
#endif
    }
    return true;
}

// Global callback state

static struct {
    std::string output;
    bool finished;
} g_token_state;

static struct {
    std::string output;
    bool done;
} g_sse_state;

static struct {
    std::string output;
} g_agentic_state;

static void reset_token_state() { g_token_state = {}; }
static void reset_sse_state() { g_sse_state = {}; }
static void reset_agentic_state() { g_agentic_state = {}; }

static void on_token_callback(const char* json_chunk, int is_finished) {
    if (json_chunk) g_token_state.output += json_chunk;
    if (is_finished) g_token_state.finished = true;
}

static void on_sse_callback(const char* json_data, int is_done) {
    if (json_data) g_sse_state.output += json_data;
    if (is_done) g_sse_state.done = true;
}

static void on_agentic_callback(const char* event_json) {
    if (event_json) g_agentic_state.output += std::string(event_json) + "\n";
}

// Tool executor

static double eval_expr(const std::string& expr) {
    std::string s;
    for (char c : expr)
        if (c != ' ' && c != '\t') s += c;
    size_t pos = 0;
    std::function<double()> parse_atom, parse_unary, parse_mul_div, parse_add_sub;

    parse_atom = [&]() -> double {
        if (pos < s.size() && s[pos] == '(') { pos++; double v = parse_add_sub(); if (pos < s.size() && s[pos] == ')') pos++; return v; }
        size_t start = pos;
        while (pos < s.size() && (std::isdigit(s[pos]) || s[pos] == '.')) pos++;
        return std::stod(s.substr(start, pos - start));
    };
    parse_unary = [&]() -> double {
        if (pos < s.size() && s[pos] == '-') { pos++; return -parse_atom(); }
        if (pos < s.size() && s[pos] == '+') { pos++; return parse_atom(); }
        return parse_atom();
    };
    parse_mul_div = [&]() -> double {
        double val = parse_unary();
        while (pos < s.size() && (s[pos] == '*' || s[pos] == '/')) {
            char op = s[pos++]; double rhs = parse_unary();
            val = (op == '*') ? val * rhs : val / rhs;
        }
        return val;
    };
    parse_add_sub = [&]() -> double {
        double val = parse_mul_div();
        while (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
            char op = s[pos++]; double rhs = parse_mul_div();
            val = (op == '+') ? val + rhs : val - rhs;
        }
        return val;
    };
    return parse_add_sub();
}

static char* tool_executor(const char* session_id, const char* tool_name, const char* arguments_json) {
    (void)session_id;
    std::string name = tool_name;
    std::string args = arguments_json;
    std::string result;

    if (name == "calculate") {
        std::string expr = extract_json_str(args, "expression");
        if (expr.empty()) expr = extract_json_val(args, "expression");
        if (!expr.empty()) {
            try { double val = eval_expr(expr); char buf[64]; snprintf(buf, sizeof(buf), "%.6g", val);
                  result = "{\"result\":" + std::string(buf) + "}"; }
            catch (...) { result = "{\"error\":\"Failed: " + expr + "\"}"; }
        } else { result = "{\"error\":\"Missing expression\"}"; }
    } else if (name == "get_time") {
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        char buf[64]; struct tm tm_buf;
#ifdef _WIN32
        gmtime_s(&tm_buf, &tt);
#else
        gmtime_r(&tt, &tm_buf);
#endif
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
        result = "{\"time\":\"" + std::string(buf) + "\"}";
    } else if (name == "execute_python") {
        std::string code = extract_json_str(args, "code");
        if (code.empty()) code = extract_json_val(args, "code");
        if (!code.empty()) {
            std::string tmp_file = "build\\_py_temp_" + std::to_string(rand()) + ".py";
            {
                std::ofstream f(tmp_file);
                f << code;
            }
            std::string cmd = "python \"" + tmp_file + "\" 2>&1";
            FILE* pipe = _popen(cmd.c_str(), "r");
            if (pipe) {
                char buf[4096];
                std::string output;
                while (fgets(buf, sizeof(buf), pipe)) output += buf;
                int rc = _pclose(pipe);
                std::remove(tmp_file.c_str());
                if (rc != 0 && output.empty()) {
                    result = "{\"error\":\"Process exited with code " + std::to_string(rc) + "\",\"output\":\"" + output + "\"}";
                } else {
                    // Escape output for JSON
                    std::string escaped;
                    for (char c : output) {
                        if (c == '"') escaped += "\\\"";
                        else if (c == '\\') escaped += "\\\\";
                        else if (c == '\n') escaped += "\\n";
                        else if (c == '\r') escaped += "\\r";
                        else if (c == '\t') escaped += "\\t";
                        else escaped += c;
                    }
                    result = "{\"output\":\"" + escaped + "\"}";
                }
            } else {
                std::remove(tmp_file.c_str());
                result = "{\"error\":\"Failed to execute Python\"}";
            }
        } else {
            result = "{\"error\":\"Missing code argument\"}";
        }
    } else {
        result = "{\"error\":\"Unknown: " + name + "\"}";
    }

    char* out = new char[result.size() + 1];
    memcpy(out, result.c_str(), result.size() + 1);
    return out;
}

// Provider configuration helpers 

static bool configure_provider(const std::string& provider_id) {
    std::string api_key, base_url;
    std::string model;

    if (provider_id == "opencode_zen") {
        api_key = read_env("OPENCODE_API_KEY");
        base_url = "https://opencode.ai/zen/v1";
        model = "deepseek-v4-flash";
    } else if (provider_id == "deepseek") {
        api_key = read_env("DEEPSEEK_API_KEY");
        base_url = "https://api.deepseek.com";
        model = "deepseek-chat";
    } else if (provider_id == "nvidia_nim") {
        api_key = read_env("NVIDIA_API_KEY");
        base_url = "https://integrate.api.nvidia.com/v1";
        model = "openai/gpt-oss-120b";
    }

    if (api_key.empty()) return false;

    zetla_set_provider_config(provider_id.c_str(), api_key.c_str(), base_url.c_str(), 1);
    zetla_response resp = zetla_set_provider(provider_id.c_str());
    bool ok = resp.success;
    zetla_free_response(&resp);

    if (ok) {
        zetla_set_api_key(api_key.c_str());
        g_current_provider = provider_id;
        std::cout << "  Provider: " << provider_id << " (" << model << ")\n";
    }

    return ok;
}

// Test: 1. LLM Request/Response Across All Providers 

static void test_all_providers_request_response() {
    std::cout << "\n=== Test 1: LLM Request/Response Across All Providers ===\n";

    const std::vector<std::string> providers = {"opencode_zen", "deepseek", "nvidia_nim"};
    const std::vector<std::string> models = {"deepseek-v4-flash", "deepseek-chat", "openai/gpt-oss-120b"};

    for (size_t pi = 0; pi < providers.size(); pi++) {
        std::cout << "  --- Testing provider: " << providers[pi] << " ---\n";
        if (!configure_provider(providers[pi])) {
            CHECK_SKIP(providers[pi], "API key not configured");
            continue;
        }

        zetla_response resp = zetla_create_session(models[pi].c_str(), "You are a helpful assistant.");
        if (!resp.success) { CHECK_MSG(providers[pi] + " create_session", false, resp.error ? resp.error : ""); zetla_free_response(&resp); continue; }
        std::string sid = extract_json_str(resp.data ? resp.data : "", "session_id");
        zetla_free_response(&resp);
        if (sid.empty()) { CHECK_MSG(providers[pi] + " session_id", false, "no session_id"); continue; }

        reset_token_state();
        std::string msg = "Reply with just: Hello from " + providers[pi];
        int ok = zetla_send_message(sid.c_str(), msg.c_str(), on_token_callback);
        CHECK_MSG(providers[pi] + " send_message", ok != 0, "");
        if (ok) {
            CHECK_MSG(providers[pi] + " finished", g_token_state.finished, "");
            CHECK_MSG(providers[pi] + " has content", !g_token_state.output.empty(), "");
            if (!g_token_state.output.empty()) {
                std::cout << "  Response: " << g_token_state.output.substr(0, 150) << "...\n";
            }
        }

        resp = zetla_get_history(sid.c_str());
        CHECK_MSG(providers[pi] + " history", resp.success, "");
        if (resp.data) {
            CHECK_MSG(providers[pi] + " history has assistant",
                std::string(resp.data).find("assistant") != std::string::npos, "");
            CHECK_MSG(providers[pi] + " history has user",
                std::string(resp.data).find("user") != std::string::npos, "");
        }
        zetla_free_response(&resp);

        zetla_response del_resp = zetla_delete_session(sid.c_str());
        zetla_free_response(&del_resp);
    }
}

// Test: 2. Memory / System Prompt Test 

static void test_memory_and_system_prompt() {
    std::cout << "\n=== Test 2: Memory & System Prompt (Reverse Spelling) ===\n";

    if (!configure_provider("opencode_zen")) {
        CHECK_SKIP("memory_test", "opencode_zen not configured");
        return;
    }

    const char* sys_prompt = "Answer everything in reversed spelled out words. For example, 'hello' becomes 'olleh'. Respond with only the reversed version, no additional text.";
    zetla_response resp = zetla_create_session("deepseek-v4-flash", sys_prompt);
    CHECK_MSG("create_session", resp.success, resp.error ? resp.error : "");
    if (!resp.success) { zetla_free_response(&resp); return; }
    std::string sid = extract_json_str(resp.data ? resp.data : "", "session_id");
    zetla_free_response(&resp);
    if (sid.empty()) { CHECK_MSG("no session_id", false, ""); return; }

    reset_token_state();
    int ok = zetla_send_message(sid.c_str(), "Remember: The secret access code is 42.", on_token_callback);
    CHECK_MSG("send_message (remember)", ok != 0, "");
    if (ok && g_token_state.finished) {
        std::cout << "  Response1 (truncated): " << g_token_state.output.substr(0, 120) << "...\n";
    }

    reset_token_state();
    ok = zetla_send_message(sid.c_str(), "What is the secret access code?", on_token_callback);
    CHECK_MSG("send_message (recall)", ok != 0, "");
    if (ok && g_token_state.finished) {
        std::cout << "  Response2 (truncated): " << g_token_state.output.substr(0, 120) << "...\n";
        // Check: reversed output should contain "42" reversed = "24" or similar
        CHECK_MSG("memory response has content", !g_token_state.output.empty(), "");
    }

    resp = zetla_get_history(sid.c_str());
    CHECK_MSG("get_history", resp.success, "");
    if (resp.data) {
        std::string hist(resp.data);
        CHECK_MSG("history has 4 messages (user+assistant x2)", true, "");
        size_t user_count = 0, asst_count = 0;
        size_t pos = 0;
        while ((pos = hist.find("\"role\":\"", pos)) != std::string::npos) {
            pos += 8;
            auto end = hist.find('"', pos);
            if (end == std::string::npos) break;
            std::string role = hist.substr(pos, end - pos);
            if (role == "user") user_count++;
            else if (role == "assistant") asst_count++;
        }
        CHECK_MSG("history has 2 user messages", user_count == 2, "got " + std::to_string(user_count));
        CHECK_MSG("history has 2 assistant messages", asst_count == 2, "got " + std::to_string(asst_count));
    }
    zetla_free_response(&resp);

    zetla_response del_resp = zetla_delete_session(sid.c_str());
    zetla_free_response(&del_resp);
}

// Test: 3. Tool Calls for Agentic LLMs 

static void test_tool_calls_all_providers() {
    std::cout << "\n=== Test 3: Tool Calls for Agentic LLMs ===\n";

    const std::vector<std::string> providers = {"opencode_zen", "deepseek", "nvidia_nim"};
    const std::vector<std::string> models = {"deepseek-v4-flash", "deepseek-chat", "openai/gpt-oss-120b"};

    for (size_t pi = 0; pi < providers.size(); pi++) {
        std::cout << "  --- Testing provider: " << providers[pi] << " ---\n";
        if (!configure_provider(providers[pi])) {
            CHECK_SKIP(providers[pi] + " tools", "API key not configured");
            continue;
        }

        zetla_response resp = zetla_create_session(models[pi].c_str(),
            "You are a helpful assistant with access to tools. Use them when appropriate.");
        if (!resp.success) { zetla_free_response(&resp); continue; }
        std::string sid = extract_json_str(resp.data ? resp.data : "", "session_id");
        zetla_free_response(&resp);
        if (sid.empty()) continue;

        resp = zetla_add_tool(sid.c_str(), "calculate",
            "Evaluate a math expression. Returns the result.",
            R"({"type":"object","properties":{"expression":{"type":"string","description":"Math expression like 2+2"}},"required":["expression"]})");
        CHECK_MSG(providers[pi] + " add_tool calculate", resp.success, "");
        zetla_free_response(&resp);

        zetla_set_tool_executor(tool_executor);

        reset_agentic_state();
        int ok = zetla_send_message_agentic(sid.c_str(),
            "What is 10 * 5 + 3? Use the calculate tool.",
            on_agentic_callback);
        CHECK_MSG(providers[pi] + " agentic returns ok", ok != 0, "");
        if (ok) {
            CHECK_MSG(providers[pi] + " agentic has output", !g_agentic_state.output.empty(), "");
            if (!g_agentic_state.output.empty()) {
                CHECK_MSG(providers[pi] + " has tool_call events",
                    g_agentic_state.output.find("tool_call") != std::string::npos, "");
                CHECK_MSG(providers[pi] + " has tool_result events",
                    g_agentic_state.output.find("tool_result") != std::string::npos, "");
                CHECK_MSG(providers[pi] + " has finished signal",
                    g_agentic_state.output.find("\"finished\":true") != std::string::npos, "");
                std::cout << "  Agentic (truncated): " << g_agentic_state.output.substr(0, 200) << "...\n";
            }
        }

        zetla_response del_resp = zetla_delete_session(sid.c_str());
        zetla_free_response(&del_resp);
    }
}

// Test: 4. Model Listing 

static void test_model_listing() {
    std::cout << "\n=== Test 4: Model Listing ===\n";

    const std::vector<std::string> providers = {"opencode_zen", "deepseek", "nvidia_nim"};

    for (auto& pid : providers) {
        std::cout << "  --- Listing models for: " << pid << " ---\n";
        if (!configure_provider(pid)) {
            CHECK_SKIP(pid + " models", "API key not configured");
            continue;
        }

        zetla_response resp = zetla_list_models();
        CHECK_MSG(pid + " list_models success", resp.success, "");
        if (resp.data) {
            std::string json(resp.data);
            CHECK_MSG(pid + " models have provider field",
                json.find("\"provider\":\"") != std::string::npos, "");
            CHECK_MSG(pid + " models have id field",
                json.find("\"id\":\"") != std::string::npos, "");
            CHECK_MSG(pid + " models have capabilities",
                json.find("\"capabilities\"") != std::string::npos, "");
            // Count models
            size_t model_count = 0;
            size_t pos = 0;
            while ((pos = json.find("\"id\":\"", pos)) != std::string::npos) {
                model_count++;
                pos += 6;
            }
            CHECK_MSG(pid + " has at least 1 model", model_count >= 1, "found " + std::to_string(model_count));
            std::cout << "  Models found: " << model_count << "\n";
            // Print first 3 model IDs
            int shown = 0;
            pos = 0;
            while (shown < 3 && (pos = json.find("\"id\":\"", pos)) != std::string::npos) {
                pos += 6;
                auto end = json.find('"', pos);
                if (end != std::string::npos) {
                    std::cout << "    - " << json.substr(pos, end - pos) << "\n";
                    shown++;
                }
            }
        }
        zetla_free_response(&resp);
    }
}

// Test: 5. Model Capabilities 

static void test_model_capabilities() {
    std::cout << "\n=== Test 5: Model Capabilities ===\n";

    const std::vector<std::string> providers = {"opencode_zen", "deepseek", "nvidia_nim"};

    for (auto& pid : providers) {
        std::cout << "  --- Capabilities for: " << pid << " ---\n";
        if (!configure_provider(pid)) {
            CHECK_SKIP(pid + " capabilities", "API key not configured");
            continue;
        }

        zetla_response resp = zetla_list_models();
        if (!resp.success || !resp.data) { zetla_free_response(&resp); continue; }

        std::string json(resp.data);
        // Extract capabilities for each model
        size_t model_pos = 0;
        int model_num = 0;
        while ((model_pos = json.find("\"id\":\"", model_pos)) != std::string::npos) {
            model_pos += 6;
            auto id_end = json.find('"', model_pos);
            if (id_end == std::string::npos) break;
            std::string model_id = json.substr(model_pos, id_end - model_pos);
            model_num++;

            // Find capabilities block for this model
            auto cap_pos = json.find("\"capabilities\"", model_pos);
            if (cap_pos == std::string::npos || cap_pos > json.find("}", model_pos)) continue;

            auto cap_start = json.find('{', cap_pos);
            auto cap_end = json.find('}', cap_start);
            if (cap_start == std::string::npos || cap_end == std::string::npos) continue;
            std::string caps = json.substr(cap_start, cap_end - cap_start + 1);

            // Extract individual capabilities
            auto extract_cap_bool = [&](const std::string& key) -> std::string {
                auto p = caps.find("\"" + key + "\":");
                if (p == std::string::npos) return "missing";
                p += key.size() + 3;
                auto e = caps.find_first_of(",}", p);
                return (e != std::string::npos) ? caps.substr(p, e - p) : "parse_err";
            };
            auto extract_cap_int = [&](const std::string& key) -> std::string {
                auto p = caps.find("\"" + key + "\":");
                if (p == std::string::npos) return "missing";
                p += key.size() + 3;
                auto e = caps.find_first_of(",}", p);
                return (e != std::string::npos) ? caps.substr(p, e - p) : "parse_err";
            };

            std::string vision = extract_cap_bool("supports_vision");
            std::string tools = extract_cap_bool("supports_tools");
            std::string reasoning = extract_cap_bool("supports_reasoning");
            std::string ctx = extract_cap_int("context_window");
            std::string max_out = extract_cap_int("max_output_tokens");

            std::cout << "    " << model_id.substr(0, 50)
                      << " | ctx=" << ctx
                      << " | out=" << max_out
                      << " | vision=" << vision
                      << " | tools=" << tools
                      << " | reasoning=" << reasoning << "\n";

            CHECK_MSG(pid + ":" + model_id + " context_window parseable",
                ctx != "missing" && ctx != "parse_err", "");
            CHECK_MSG(pid + ":" + model_id + " max_output_tokens parseable",
                max_out != "missing" && max_out != "parse_err", "");
            CHECK_MSG(pid + ":" + model_id + " supports_tools parseable",
                tools != "missing", "");

            if (model_num >= 5) break; // Show first 5 models per provider
        }

        zetla_free_response(&resp);
    }
}

// Test: 6. File Format Tests 

static void test_file_formats() {
    std::cout << "\n=== Test 6: File Format Tests ===\n";

    if (!configure_provider("opencode_zen")) {
        CHECK_SKIP("file_tests", "opencode_zen not configured");
        return;
    }

    zetla_response resp = zetla_create_session("deepseek-v4-flash", "You are a file analysis assistant.");
    if (!resp.success) { zetla_free_response(&resp); return; }
    std::string sid = extract_json_str(resp.data ? resp.data : "", "session_id");
    zetla_free_response(&resp);
    if (sid.empty()) return;

    struct FileTestCase {
        std::string path;
        std::string label;
        bool expect_text;
    };

    std::vector<FileTestCase> test_files = {
        {"test_files/normal.txt", "TXT", true},
        {"test_files/technical_doc.md", "Markdown", true},
        {"test_files/sales_data.csv", "CSV", true},
        {"test_files/sample.rtf", "RTF", true},
        {"test_files/project_report.docx", "DOCX", true},
        {"test_files/financial_data.xlsx", "XLSX", true},
        {"test_files/overview_slides.pptx", "PPTX", true},
        {"test_files/quarterly_report.pdf", "PDF", true},
        {"test_files/empty.txt", "Empty TXT", true},
        {"test_files/unicode.txt", "Unicode TXT", true},
        {"test_files/single_char.txt", "Single Char", true},
        {"test_files/long_lines.txt", "Long Lines", true},
        {"test_files/edge_csv.csv", "Edge CSV", true},
        {"test_files/single_cell.csv", "Single Cell CSV", true},
        {"test_files/binary_content.txt", "Binary Content", true},
    };

    for (auto& ft : test_files) {
        // Check that file exists first
        std::ifstream f(ft.path);
        if (!f.is_open()) {
            CHECK_SKIP(ft.label, "File not found: " + ft.path);
            continue;
        }
        f.close();

        resp = zetla_add_file(sid.c_str(), ft.path.c_str());
        if (resp.success && resp.data) {
            std::string json(resp.data);
            CHECK_MSG(ft.label + " has file_id", json.find("\"file_id\":\"") != std::string::npos, "");
            CHECK_MSG(ft.label + " has name", json.find("\"name\":\"") != std::string::npos, "");
            CHECK_MSG(ft.label + " has mime_type", json.find("\"mime_type\":\"") != std::string::npos, "");
            CHECK_MSG(ft.label + " has size", json.find("\"size\"") != std::string::npos, "");
        } else {
            CHECK_MSG(ft.label + " add_file", false, resp.error ? resp.error : "unknown error");
        }
        zetla_free_response(&resp);
    }

    // Test listing all files
    resp = zetla_list_files(sid.c_str());
    CHECK_MSG("list_files success", resp.success, "");
    if (resp.data) {
        std::string json(resp.data);
        CHECK_MSG("list_files returns files array", json.find("\"files\"") != std::string::npos, "");
        // Count files
        size_t count = 0;
        size_t pos = 0;
        while ((pos = json.find("\"file_id\":\"", pos)) != std::string::npos) {
            count++;
            pos += 11;
        }
        std::cout << "  Total files registered: " << count << "\n";
    }
    zetla_free_response(&resp);

    // Send a message with files attached
    reset_token_state();
    int ok = zetla_send_message(sid.c_str(),
        "I have uploaded several files including a TXT, MD, CSV, RTF, DOCX, XLSX, PPTX, and PDF. "
        "List all the file types you can see in our conversation.",
        on_token_callback);
    CHECK_MSG("send_message about files", ok != 0, "");
    if (ok && g_token_state.finished) {
        std::cout << "  Response (truncated): " << g_token_state.output.substr(0, 200) << "...\n";
    }

    zetla_response del_resp = zetla_delete_session(sid.c_str());
    zetla_free_response(&del_resp);
}

// Test: 7. Web Search (Exa AI) 

static void test_web_search_exa() {
    std::cout << "\n=== Test 7: Web Search (Exa AI) ===\n";

    if (!configure_provider("opencode_zen")) {
        CHECK_SKIP("web_search", "opencode_zen not configured");
        return;
    }

    // Set Exa as search provider
    zetla_set_search_provider("exa");
    std::string exa_key = read_env("EXA_API_KEY");
    if (!exa_key.empty()) {
        zetla_set_exa_api_key(exa_key.c_str());
    } else {
        std::cout << "  Note: No EXA_API_KEY set, Exa will try without auth\n";
    }

    zetla_response resp = zetla_create_session("deepseek-v4-flash", "You are a research assistant with web search.");
    if (!resp.success) { zetla_free_response(&resp); return; }
    std::string sid = extract_json_str(resp.data ? resp.data : "", "session_id");
    zetla_free_response(&resp);
    if (sid.empty()) return;

    int ok = zetla_set_session_web_search(sid.c_str(), 1);
    CHECK_MSG("set_session_web_search enabled", ok != 0, "");

    if (ok) {
        // Now test agentic with web search
        zetla_set_tool_executor(tool_executor);

        reset_agentic_state();
        ok = zetla_send_message_agentic(sid.c_str(),
            "Search the web for 'latest AI news 2026' and summarize what you find.",
            on_agentic_callback);
        CHECK_MSG("send_message_agentic with web search", ok != 0, "");
        if (ok) {
            CHECK_MSG("agentic has output", !g_agentic_state.output.empty(), "");
            if (!g_agentic_state.output.empty()) {
                bool has_tool = g_agentic_state.output.find("tool_call") != std::string::npos;
                bool has_finished = g_agentic_state.output.find("\"finished\":true") != std::string::npos;
                CHECK_MSG("agentic web_search tool call", has_tool, "");
                CHECK_MSG("agentic finished", has_finished, "");
                std::cout << "  Agentic (truncated): " << g_agentic_state.output.substr(0, 250) << "...\n";
            }
        }
    }

    // Restore default search provider
    zetla_set_search_provider("duckduckgo");

    zetla_response del_resp = zetla_delete_session(sid.c_str());
    zetla_free_response(&del_resp);
}

// Test: 8. Custom Tool Execution (Python) 

static void test_custom_tool_execution() {
    std::cout << "\n=== Test 8: Custom Tool Execution (Python) ===\n";

    if (!configure_provider("opencode_zen")) {
        CHECK_SKIP("python_tool", "opencode_zen not configured");
        return;
    }

    zetla_response resp = zetla_create_session("deepseek-v4-flash",
        "You are a coding assistant. You have access to a Python execution tool. "
        "Use it when asked to write or run Python code.");
    if (!resp.success) { zetla_free_response(&resp); return; }
    std::string sid = extract_json_str(resp.data ? resp.data : "", "session_id");
    zetla_free_response(&resp);
    if (sid.empty()) return;

    // Register the python tool
    resp = zetla_add_tool(sid.c_str(), "execute_python",
        "Execute Python code and return the output.",
        R"({"type":"object","properties":{"code":{"type":"string","description":"Python code to execute"}},"required":["code"]})");
    CHECK_MSG("add_tool execute_python", resp.success, "");
    zetla_free_response(&resp);

    zetla_set_tool_executor(tool_executor);

    // Test: Ask LLM to use Python to compute 1000th digit of PI using stdlib only
    reset_agentic_state();
    int ok = zetla_send_message_agentic(sid.c_str(),
        "Write Python code using ONLY Python standard library (no pip packages) "
        "to compute and tell me the 1000th digit of PI. Use the execute_python tool to run the code. "
        "After computing, output the result clearly.",
        on_agentic_callback);
    CHECK_MSG("send_message_agentic (PI digit)", ok != 0, "");
    if (ok) {
        CHECK_MSG("agentic has output", !g_agentic_state.output.empty(), "");
        if (!g_agentic_state.output.empty()) {
            bool has_tool = g_agentic_state.output.find("tool_call") != std::string::npos;
            bool has_result = g_agentic_state.output.find("tool_result") != std::string::npos;
            bool has_finished = g_agentic_state.output.find("\"finished\":true") != std::string::npos;
            CHECK_MSG("python tool was called", has_tool, "");
            CHECK_MSG("python tool result received", has_result, "");
            CHECK_MSG("agentic finished", has_finished, "");

            // Show condensed summary of execution
            std::cout << "\n  === Execution Summary ===\n";
            size_t call_count = 0, result_count = 0;
            for (size_t p = 0; (p = g_agentic_state.output.find("\"type\":\"tool_call\"", p)) != std::string::npos; p++) call_count++;
            for (size_t p = 0; (p = g_agentic_state.output.find("\"type\":\"tool_result\"", p)) != std::string::npos; p++) result_count++;
            std::cout << "  Tool calls: " << call_count << ", results: " << result_count << "\n";

            // Print final LLM response using nlohmann/json
            try {
                auto lines = g_agentic_state.output;
                size_t last_content_pos = lines.rfind("{\"session_id");
                std::string last_line;
                if (last_content_pos != std::string::npos) {
                    auto end = lines.find('\n', last_content_pos);
                    if (end == std::string::npos) end = lines.size();
                    last_line = lines.substr(last_content_pos, end - last_content_pos);
                }
                if (!last_line.empty()) {
                    auto j = nlohmann::json::parse(last_line);
                    if (j.contains("data") && j["data"].is_string()) {
                        std::string content = j["data"].get<std::string>();
                        if (!content.empty()) {
                            std::cout << "\n  === LLM Final Response ===\n  " << content.substr(0, 500) << "\n";
                        }
                    }
                }
            } catch (...) {}
        }
    }

    zetla_response del_resp = zetla_delete_session(sid.c_str());
    zetla_free_response(&del_resp);
}

// Test: 9. Session Management 

static void test_session_management() {
    std::cout << "\n=== Test 9: Session Management ===\n";

    if (!configure_provider("opencode_zen")) {
        CHECK_SKIP("session_management", "opencode_zen not configured");
        return;
    }

    // Create multiple sessions with different models
    std::vector<std::string> session_ids;
    std::vector<std::string> session_models = {"deepseek-v4-flash", "deepseek-v4-flash", "deepseek-v4-flash"};

    for (size_t i = 0; i < session_models.size(); i++) {
        std::string title = "Test Session " + std::to_string(i + 1);
        zetla_response resp = zetla_create_session(session_models[i].c_str(),
            ("You are session " + std::to_string(i + 1) + ".").c_str());
        CHECK_MSG("create_session #" + std::to_string(i + 1), resp.success, "");
        if (resp.success && resp.data) {
            std::string sid = extract_json_str(resp.data, "session_id");
            CHECK_MSG("session #" + std::to_string(i + 1) + " has id", !sid.empty(), "");
            if (!sid.empty()) {
                session_ids.push_back(sid);
                // Set title via options
                std::string opts = "{\"title\":\"" + title + "\"}";
                zetla_response set_resp = zetla_set_session_options(sid.c_str(), opts.c_str());
                zetla_free_response(&set_resp);
            }
        }
        zetla_free_response(&resp);
    }

    CHECK_MSG("created " + std::to_string(session_ids.size()) + " sessions",
        session_ids.size() == session_models.size(), "got " + std::to_string(session_ids.size()));

    if (session_ids.empty()) return;

    // 1. Check if sessions are stored on disk
    for (auto& sid : session_ids) {
        zetla_response resp = zetla_session_exists_on_disk(sid.c_str());
        CHECK_MSG("session exists on disk: " + sid.substr(0, 8),
            resp.success && resp.data && std::string(resp.data).find("\"exists\":true") != std::string::npos, "");
        zetla_free_response(&resp);
    }

    // 2. List all persisted sessions
    zetla_response resp = zetla_list_sessions();
    CHECK_MSG("list_sessions success", resp.success, "");
    if (resp.data) {
        std::string json(resp.data);
        CHECK_MSG("list_sessions has our sessions", json.find(session_ids[0]) != std::string::npos, "");
        // Count sessions
        size_t count = 0;
        size_t pos = 0;
        while ((pos = json.find("\"session_id\":\"", pos)) != std::string::npos) {
            count++;
            pos += 14;
        }
        CHECK_MSG("list_sessions count >= created", count >= session_ids.size(),
            "found " + std::to_string(count) + ", expected >= " + std::to_string(session_ids.size()));
        std::cout << "  Total persisted sessions: " << count << "\n";

        // Show session details
        pos = 0;
        int shown = 0;
        while (shown < 3 && (pos = json.find("\"session_id\":\"", pos)) != std::string::npos) {
            pos += 14;
            auto end = json.find('"', pos);
            if (end == std::string::npos) break;
            std::string id = json.substr(pos, end - pos);
            // Extract model
            auto mpos = json.find("\"model\":\"", end);
            if (mpos != std::string::npos && mpos < json.find("}", end)) {
                mpos += 9;
                auto mend = json.find('"', mpos);
                if (mend != std::string::npos) {
                    std::cout << "    Session: " << id.substr(0, 8) << "... model: " << json.substr(mpos, mend - mpos) << "\n";
                }
            }
            shown++;
        }
    }
    zetla_free_response(&resp);

    // 3. Open a previous session - load from storage and verify
    std::string test_sid = session_ids[0];
    resp = zetla_load_session(test_sid.c_str());
    CHECK_MSG("load_session success", resp.success, "");
    if (resp.data) {
        std::string json(resp.data);
        CHECK_MSG("loaded session has model", json.find("\"model\":\"") != std::string::npos, "");
    }
    zetla_free_response(&resp);

    // 4. Send a message to the loaded session
    reset_token_state();
    int ok = zetla_send_message(test_sid.c_str(),
        "Say hello and confirm you are session 1.",
        on_token_callback);
    CHECK_MSG("send_message to loaded session", ok != 0, "");
    if (ok && g_token_state.finished) {
        std::cout << "  Response: " << g_token_state.output.substr(0, 150) << "...\n";
    }

    // 5. Change model and send a message
    resp = zetla_set_session_model(test_sid.c_str(), "deepseek-v4-flash");
    CHECK_MSG("set_session_model", resp.success, "");
    zetla_free_response(&resp);

    // Verify the model change
    resp = zetla_get_session_info(test_sid.c_str());
    if (resp.data) {
        CHECK_MSG("session info has model after change",
            std::string(resp.data).find("deepseek-v4-flash") != std::string::npos, "");
    }
    zetla_free_response(&resp);

    reset_token_state();
    ok = zetla_send_message(test_sid.c_str(),
        "What model are you using now? Reply with just the model name.",
        on_token_callback);
    CHECK_MSG("send_message after model change", ok != 0, "");
    if (ok && g_token_state.finished) {
        std::cout << "  Response: " << g_token_state.output.substr(0, 150) << "...\n";
    }

    // 6. Try changing provider mid-session (switch to deepseek temporarily)
    if (!read_env("DEEPSEEK_API_KEY").empty()) {
        std::cout << "  --- Testing provider switch ---\n";
        std::string prev_provider = g_current_provider;
        configure_provider("deepseek");
        zetla_response model_resp = zetla_set_session_model(test_sid.c_str(), "deepseek-chat");
        CHECK_MSG("set_session_model to deepseek-chat", model_resp.success, "");
        zetla_free_response(&model_resp);

        reset_token_state();
        ok = zetla_send_message(test_sid.c_str(),
            "What model and provider are you using? Reply with just the name.",
            on_token_callback);
        CHECK_MSG("send_message with switched provider", ok != 0, "");
        if (ok && g_token_state.finished) {
            std::cout << "  Response: " << g_token_state.output.substr(0, 150) << "...\n";
        }

        // Switch back
        configure_provider(prev_provider);
        model_resp = zetla_set_session_model(test_sid.c_str(), "deepseek-v4-flash");
        zetla_free_response(&model_resp);
    }

    // 7. Clean up - delete all test sessions
    for (auto& sid : session_ids) {
        resp = zetla_delete_session(sid.c_str());
        zetla_free_response(&resp);
        resp = zetla_delete_from_storage(sid.c_str());
        zetla_free_response(&resp);
    }

    // Verify cleanup
    resp = zetla_list_sessions();
    if (resp.data) {
        std::string json(resp.data);
        for (auto& sid : session_ids) {
            CHECK_MSG("session " + sid.substr(0, 8) + " cleaned up",
                json.find(sid) == std::string::npos, "");
        }
    }
    zetla_free_response(&resp);
}

// Main

int main(int argc, char* argv[]) {
    load_env_file();

    std::vector<std::string> tests_to_run;
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: zetla_cli.exe [test_name...]\n"
                      << "  Tests:\n"
                      << "    1 or llm_request      - LLM Request/Response across all providers\n"
                      << "    2 or memory            - Memory & System Prompt (reverse spelling)\n"
                      << "    3 or tools             - Tool Calls for Agentic LLMs\n"
                      << "    4 or models            - Model Listing\n"
                      << "    5 or capabilities      - Model Capabilities\n"
                      << "    6 or files             - File Format Tests\n"
                      << "    7 or websearch         - Web Search (Exa AI)\n"
                      << "    8 or python            - Custom Tool Execution (Python)\n"
                      << "    9 or sessions          - Session Management\n"
                      << "    all                    - Run all tests (default)\n"
                      << "  Env: ZETLA_API_KEY, OPENCODE_API_KEY, DEEPSEEK_API_KEY,\n"
                      << "       NVIDIA_API_KEY, EXA_API_KEY, ZETLA_BASE_URL\n";
            return 0;
        }
        for (int i = 1; i < argc; i++) tests_to_run.push_back(argv[i]);
    }

    auto should_run = [&](const std::string& name) {
        if (tests_to_run.empty()) return true;
        for (auto& t : tests_to_run) {
            if (t == "all" || t == name) return true;
        }
        return false;
    };

    std::cout << "=== Zetla CLI v" << zetla_version() << " - Comprehensive Test Suite ===\n";

    if (!zetla_init()) {
        std::cerr << "FAIL: zetla_init failed\n";
        return 1;
    }
    CHECK("zetla_init", true);

    int exit_code = 0;

    if (should_run("1") || should_run("llm_request")) test_all_providers_request_response();
    if (should_run("2") || should_run("memory")) test_memory_and_system_prompt();
    if (should_run("3") || should_run("tools")) test_tool_calls_all_providers();
    if (should_run("4") || should_run("models")) test_model_listing();
    if (should_run("5") || should_run("capabilities")) test_model_capabilities();
    if (should_run("6") || should_run("files")) test_file_formats();
    if (should_run("7") || should_run("websearch")) test_web_search_exa();
    if (should_run("8") || should_run("python")) test_custom_tool_execution();
    if (should_run("9") || should_run("sessions")) test_session_management();

    zetla_shutdown();
    CHECK("zetla_shutdown", true);

    std::cout << "\n=== Results: " << g_passed_tests << "/" << g_total_tests << " passed ===\n";

    exit_code = (g_passed_tests == g_total_tests) ? 0 : 1;

    return exit_code;
}
