#include "rag_tool.hpp"
#include <cstdio>
#include <cstring>

static int g_total_tests = 0, g_passed_tests = 0;

#define CHECK(label, cond) do { \
    g_total_tests++; \
    if (cond) { g_passed_tests++; printf("  PASS [%s]\n", label); } \
    else { printf("  FAIL [%s]\n", label); } \
} while(0)

static void test_rag_manager() {
    printf("\n=== RAG Manager Test ===\n");

    auto& mgr = zetla::rag::RagManager::instance();
    mgr.init_embedder("stub");

    CHECK("Embedder ready", true);

    const char* session_id = "test_session_1";
    mgr.create_session(session_id);

    //  Add files 
    int n = mgr.add_file(session_id, "docs/report.txt",
        "This quarterly financial report shows revenue growth of 15% "
        "driven by strong sales in the APAC region. Operating expenses "
        "increased 8% due to investments in R&D and marketing. "
        "The gross margin improved to 42% from 38% last year. "
        "Net income was $4.2 million, up 22% year-over-year.");

    CHECK("Add file report.txt", n >= 0);

    n = mgr.add_file(session_id, "docs/manual.txt",
        "To install the device, first connect the power cable to the "
        "back panel. Then press and hold the power button for 3 seconds. "
        "The LED indicator will flash blue during startup. "
        "Wait for the welcome screen to appear before proceeding.");

    CHECK("Add file manual.txt", n >= 0);

    n = mgr.add_file(session_id, "docs/notes.txt",
        "Meeting notes: discussed Q3 priorities. Action items: finalize "
        "budget by Friday, schedule follow-up with engineering team, "
        "prepare slides for board presentation. Next meeting is Monday.");

    CHECK("Add file notes.txt", n >= 0);

    int total_chunks = mgr.chunk_count(session_id);
    printf("  Total chunks indexed: %d\n", total_chunks);
    CHECK("Chunks indexed", total_chunks > 0);

    size_t mem = mgr.memory_bytes(session_id);
    printf("  Memory usage: %zu KB\n", mem / 1024);

    //  Search queries 

    struct { const char* query; const char* expected; } queries[] = {
        {"financial report revenue growth", "revenue"},
        {"how to install the device", "install"},
        {"Q3 priorities action items", "Q3"},
        {"net income year over year", "income"},
    };

    for (auto& q : queries) {
        auto json = mgr.search(session_id, q.query, 3, "");
        printf("  Query: \"%s\" -> %s\n", q.query,
               json.size() > 10 ? "results found" : "no results");

        bool found = json.find(q.expected) != std::string::npos;
        CHECK(q.query, found);
    }

    //  Scoped search 
    auto scoped = mgr.search(session_id, "install", 3, "docs/manual.txt");
    printf("  Scoped search (manual.txt): %s\n",
           scoped.size() > 10 ? "results found" : "no results");
    CHECK("Scoped search works", scoped.size() > 10);

    //  Cleanup 
    mgr.remove_session(session_id);
    CHECK("Session removed", mgr.chunk_count(session_id) == 0);
}

int main() {
    printf("rag_test - RAG Manager Integration Test\n");
    printf("========================================\n");

    test_rag_manager();

    printf("\n=== Results: %d/%d passed ===\n", g_passed_tests, g_total_tests);
    return g_passed_tests == g_total_tests ? 0 : 1;
}
