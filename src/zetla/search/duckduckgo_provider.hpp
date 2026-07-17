#pragma once
#include "search_provider.hpp"

namespace zetla::search {

    class DuckDuckGoSearchProvider : public ISearchProvider {
    public:
        SearchResponse search(const std::string& query, int num_results = 5) override;
        std::string name() const override { return "duckduckgo"; }
    };

}
