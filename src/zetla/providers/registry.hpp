#pragma once
#include "provider_facade.hpp"
#include "opencode/opencode_provider.hpp"
#include "deepseek/deepseek_provider.hpp"
#include "nvidia/nvidia_nim_provider.hpp"
#include "../core/types.hpp"
#include "nlohmann/json.hpp"
#include <memory>
#include <string>

namespace zetla::providers {

    struct ProviderConfig {
        std::string id;
        std::string api_key;
        std::string base_url;
        bool enabled = true;
    };

    inline std::unique_ptr<core::IProvider> create_provider(const ProviderConfig& config) {
        if (!config.enabled) return nullptr;

        if (config.id == "opencode_zen") {
            return std::make_unique<OpenCodeProvider>(
                OpenCodeProvider::configure(config.api_key, config.base_url)
            );
        } else if (config.id == "deepseek") {
            return std::make_unique<DeepseekProvider>(
                DeepseekProvider::configure(config.api_key, config.base_url)
            );
        } else if (config.id == "nvidia_nim") {
            return std::make_unique<NvidiaNimProvider>(
                NvidiaNimProvider::configure(config.api_key, config.base_url)
            );
        }
        return nullptr;
    }

    inline std::string list_available_providers() {
        nlohmann::json arr = nlohmann::json::array();
        arr.push_back({{"id", "opencode_zen"}, {"name", "OpenCode Zen"}, {"default_base_url", "https://opencode.ai/zen/v1"}});
        arr.push_back({{"id", "nvidia_nim"}, {"name", "NVIDIA NIM"}, {"default_base_url", "https://integrate.api.nvidia.com/v1"}});
        arr.push_back({{"id", "deepseek"}, {"name", "Deepseek"}, {"default_base_url", "https://api.deepseek.com"}});
        return arr.dump();
    }
}
