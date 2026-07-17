#pragma once
#include "search_provider.hpp"

namespace zetla::search {

    class ExaSearchProvider : public ISearchProvider {
    private:
        std::string api_key_;
        std::string mcp_url_;
    public:
        explicit ExaSearchProvider(const std::string& api_key = "", const std::string& mcp_url = "");
        SearchResponse search(const std::string& query, int num_results = 5) override;
        std::string name() const override { return "exa"; }
        void set_api_key(const std::string& key) { api_key_ = key; }
    };

}
