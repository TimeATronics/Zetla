#pragma once

#include <cstddef>

// DLL export/import - same pattern as Zetla
#if defined(HGNFS_STATIC)
  #define HGNFS_API
#elif defined(_WIN32) || defined(_WIN64)
  #ifdef HGNFS_DLL_EXPORTS
    #define HGNFS_API __declspec(dllexport)
  #else
    #define HGNFS_API __declspec(dllimport)
  #endif
#else
  #define HGNFS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

//  error codes 

#define HGNFS_OK        0
#define HGNFS_ERR_INIT  1
#define HGNFS_ERR_DIM   2
#define HGNFS_ERR_EMPTY 3
#define HGNFS_ERR_MEM   4

//  response struct 

typedef struct {
    int success;         // 0 = success, nonzero = error code
    char* data;          // JSON string result (caller frees via hgnfs_free_response)
    char* error;         // error message (caller frees via hgnfs_free_response)
} hgnfs_response;

//  result struct (array output) 

typedef struct {
    int count;
    char** paths;
    int* chunk_indices;
    float* scores;
    float* distances;
    char** snippets;
} hgnfs_search_results;

//  lifecycle 

HGNFS_API int hgnfs_init(const char* db_path);
HGNFS_API void hgnfs_shutdown(void);

//  Euclidean parametrization (raw float arrays) 

// Set/get Euclidean params for a file.
// z: array of dim floats. Must match the dimension of the index.
HGNFS_API hgnfs_response hgnfs_set_file_embedding(
    const char* file_path, const float* z, int dim);

HGNFS_API hgnfs_response hgnfs_get_file_embedding(
    const char* file_path, int dim);

//  add chunks 

// Add a single chunk with pre-computed Euclidean parametrization
HGNFS_API hgnfs_response hgnfs_add_chunk(
    const char* file_path, int chunk_idx,
    const float* z, int dim);

// Batch add chunks: Z is flat array of n*dim floats
HGNFS_API hgnfs_response hgnfs_add_chunks_batch(
    const char** file_paths, const int* chunk_indices,
    int n, const float* Z, int dim);

//  search 

// Query the index. query_z: dim floats (Euclidean param of query text).
HGNFS_API hgnfs_response hgnfs_search(
    const float* query_z, int dim,
    int top_k,
    const char* scope_path);

// search returning structured results
HGNFS_API hgnfs_search_results hgnfs_search_structured(
    const float* query_z, int dim,
    int top_k,
    const char* scope_path);

//  info 

HGNFS_API int hgnfs_n_chunks(void);
HGNFS_API size_t hgnfs_memory_bytes(void);

//  memory management 

HGNFS_API void hgnfs_free_response(hgnfs_response* resp);
HGNFS_API void hgnfs_free_search_results(hgnfs_search_results* results);

#ifdef __cplusplus
}
#endif
