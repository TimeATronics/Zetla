#include "hgnfs_api.h"
#include "../index/lorentz_index.hpp"
#include "../core/types.hpp"
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <sqlite3.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

//  global state 

static std::unique_ptr<hgnfs::index::LorentzIndex> g_index;
static sqlite3* g_db = nullptr;

namespace {

hgnfs_response make_ok(const std::string& json_data) {
    hgnfs_response r{};
    r.success = HGNFS_OK;
    r.data = (char*)malloc(json_data.size() + 1);
    if (r.data) {
        std::memcpy(r.data, json_data.c_str(), json_data.size() + 1);
    }
    return r;
}

hgnfs_response make_err(int code, const std::string& msg) {
    hgnfs_response r{};
    r.success = code;
    r.error = (char*)malloc(msg.size() + 1);
    if (r.error) {
        std::memcpy(r.error, msg.c_str(), msg.size() + 1);
    }
    return r;
}

void create_table(sqlite3* db) {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS file_embeddings ("
        "  path TEXT PRIMARY KEY,"
        "  dim INTEGER NOT NULL,"
        "  z BLOB NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS chunks ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  path TEXT NOT NULL,"
        "  chunk_idx INTEGER NOT NULL,"
        "  dim INTEGER NOT NULL,"
        "  z BLOB NOT NULL"
        ");";
    sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
}

}  // anonymous namespace

//  lifecycle 

HGNFS_API int hgnfs_init(const char* db_path) {
    g_index = std::make_unique<hgnfs::index::LorentzIndex>(hgnfs::DEFAULT_DIM);
    if (db_path && db_path[0]) {
        int rc = sqlite3_open(db_path, &g_db);
        if (rc == SQLITE_OK) {
            create_table(g_db);
        }
    }
    return HGNFS_OK;
}

HGNFS_API void hgnfs_shutdown(void) {
    g_index.reset();
    if (g_db) { sqlite3_close(g_db); g_db = nullptr; }
}

//  file embeddings 

HGNFS_API hgnfs_response hgnfs_set_file_embedding(
    const char* file_path, const float* z, int dim) {
    if (!g_index) return make_err(HGNFS_ERR_INIT, "not initialized");
    if (g_db) {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(g_db,
            "INSERT OR REPLACE INTO file_embeddings (path, dim, z) VALUES (?, ?, ?)",
            -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, file_path, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, dim);
        sqlite3_bind_blob(stmt, 3, z, dim * sizeof(float), SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    return make_ok("{}");
}

HGNFS_API hgnfs_response hgnfs_get_file_embedding(
    const char* file_path, int dim) {
    if (!g_db) return make_err(HGNFS_ERR_INIT, "no database");
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(g_db,
        "SELECT z FROM file_embeddings WHERE path = ?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, file_path, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int blob_size = sqlite3_column_bytes(stmt, 0);
        json j;
        j["path"] = file_path;
        j["dim"] = dim;
        j["z"] = std::vector<float>(
            (const float*)sqlite3_column_blob(stmt, 0),
            (const float*)sqlite3_column_blob(stmt, 0) + blob_size / sizeof(float)
        );
        sqlite3_finalize(stmt);
        return make_ok(j.dump());
    }
    sqlite3_finalize(stmt);
    return make_err(HGNFS_ERR_EMPTY, "file not found");
}

//  add chunks 

HGNFS_API hgnfs_response hgnfs_add_chunk(
    const char* file_path, int chunk_idx,
    const float* z, int dim) {
    if (!g_index) return make_err(HGNFS_ERR_INIT, "not initialized");
    g_index->add(z, dim, {file_path, chunk_idx});
    return make_ok("{}");
}

HGNFS_API hgnfs_response hgnfs_add_chunks_batch(
    const char** file_paths, const int* chunk_indices,
    int n, const float* Z, int dim) {
    if (!g_index) return make_err(HGNFS_ERR_INIT, "not initialized");
    std::vector<hgnfs::ChunkMeta> metas(n);
    for (int i = 0; i < n; ++i) {
        metas[i].path = file_paths[i];
        metas[i].chunk_idx = chunk_indices[i];
    }
    g_index->add_batch(Z, n, dim, metas);
    return make_ok("{}");
}

//  search 

HGNFS_API hgnfs_response hgnfs_search(
    const float* query_z, int dim, int top_k, const char* scope_path) {
    if (!g_index) return make_err(HGNFS_ERR_INIT, "not initialized");
    auto results = g_index->search(query_z, dim, top_k, scope_path);
    json j = json::array();
    for (auto& r : results) {
        json item;
        item["chunk_idx"] = r.chunk_idx;
        item["path"] = r.path;
        item["score"] = r.score;
        item["distance"] = r.distance;
        j.push_back(item);
    }
    return make_ok(j.dump());
}

HGNFS_API hgnfs_search_results hgnfs_search_structured(
    const float* query_z, int dim, int top_k, const char* scope_path) {
    hgnfs_search_results r{};
    if (!g_index) return r;
    auto results = g_index->search(query_z, dim, top_k, scope_path);
    r.count = static_cast<int>(results.size());
    r.paths = (char**)malloc(r.count * sizeof(char*));
    r.chunk_indices = (int*)malloc(r.count * sizeof(int));
    r.scores = (float*)malloc(r.count * sizeof(float));
    r.distances = (float*)malloc(r.count * sizeof(float));
    r.snippets = (char**)malloc(r.count * sizeof(char*));
    for (int i = 0; i < r.count; ++i) {
        r.paths[i] = (char*)malloc(results[i].path.size() + 1);
        std::memcpy(r.paths[i], results[i].path.c_str(), results[i].path.size() + 1);
        r.chunk_indices[i] = results[i].chunk_idx;
        r.scores[i] = results[i].score;
        r.distances[i] = results[i].distance;
        r.snippets[i] = (char*)malloc(results[i].text_snippet.size() + 1);
        std::memcpy(r.snippets[i], results[i].text_snippet.c_str(),
                    results[i].text_snippet.size() + 1);
    }
    return r;
}

//  info 

HGNFS_API int hgnfs_n_chunks(void) {
    return g_index ? g_index->n_chunks() : 0;
}

HGNFS_API size_t hgnfs_memory_bytes(void) {
    return g_index ? g_index->memory_bytes() : 0;
}

//  memory management 

HGNFS_API void hgnfs_free_response(hgnfs_response* resp) {
    if (!resp) return;
    free(resp->data);
    free(resp->error);
    resp->data = nullptr;
    resp->error = nullptr;
}

HGNFS_API void hgnfs_free_search_results(hgnfs_search_results* r) {
    if (!r) return;
    for (int i = 0; i < r->count; ++i) {
        free(r->paths[i]);
        free(r->snippets[i]);
    }
    free(r->paths);
    free(r->chunk_indices);
    free(r->scores);
    free(r->distances);
    free(r->snippets);
    r->count = 0;
}
