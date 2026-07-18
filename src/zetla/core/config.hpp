#pragma once
#include <string>
#include <map>
#include <cstdlib>
#include "types.hpp"

namespace zetla::core {

    struct ProviderConfig {
        std::string api_key;
        std::string base_url;
        bool enabled = true;
    };

    struct ApiConfig {
        std::string api_key;
        std::string storage_path = "~/.zetla/sessions";
        std::string system_prompt;
        ChatOptions default_options;
        std::map<std::string, ProviderConfig> providers;
        std::string search_provider = "exa";
        std::string exa_api_key;
        std::string exa_mcp_url = "https://mcp.exa.ai/mcp";
    };

    inline ApiConfig& get_config() {
        static ApiConfig config;
        static bool initialized = false;
        if (!initialized) {
            const char* env_key = std::getenv("ZETLA_API_KEY");
            if (env_key) {
                config.api_key = env_key;
            }
            initialized = true;
        }
        return config;
    }

    inline void set_api_config(const ApiConfig& cfg) {
        get_config() = cfg;
    }

    inline const std::string& get_api_key() {
        return get_config().api_key;
    }

    inline ChatOptions& get_default_options() {
        return get_config().default_options;
    }

    inline const std::string& get_system_prompt() {
        return get_config().system_prompt;
    }
}
