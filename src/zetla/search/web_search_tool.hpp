#pragma once
#include "../core/provider.hpp"
#include "../network/http_client.hpp"
#include "search_provider.hpp"
#include <memory>

namespace zetla::search {

    class WebSearchTool : public core::IToolExecutor {
    private:
        std::unique_ptr<ISearchProvider> search_provider_;
        int num_results_;

        core::ToolCallResult do_search(const std::string& query, const std::string& tool_call_id);
        core::ToolCallResult do_fetch(const std::string& url, const std::string& tool_call_id);

    public:
        explicit WebSearchTool(std::unique_ptr<ISearchProvider> provider, int num_results = 5);

        std::string name() const override;
        std::string description() const override;
        std::string parameters_schema() const override;
        core::ToolCallResult execute(const core::ToolCallRequest& call) override;
    };

}
