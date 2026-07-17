#pragma once
#include "../core/provider.hpp"
#include "search_provider.hpp"
#include <memory>

namespace zetla::search {

    class WebSearchTool : public core::IToolExecutor {
    private:
        std::unique_ptr<ISearchProvider> search_provider_;
        int num_results_;

    public:
        explicit WebSearchTool(std::unique_ptr<ISearchProvider> provider, int num_results = 5);

        std::string name() const override;
        std::string description() const override;
        std::string parameters_schema() const override;
        core::ToolCallResult execute(const core::ToolCallRequest& call) override;
    };

}
