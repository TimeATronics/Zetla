#pragma once
#include <string>
#include <vector>

namespace zetla::search {

    struct SearchResult {
        std::string title;
        std::string url;
        std::string snippet;
    };

    struct SearchResponse {
        std::vector<SearchResult> results;
        std::string error;
        bool success = true;
    };

    class ISearchProvider {
    public:
        virtual ~ISearchProvider() = default;
        virtual SearchResponse search(const std::string& query, int num_results = 5) = 0;
        virtual std::string name() const = 0;
    };

}
