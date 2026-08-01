#include "loader.hpp"
#include <cstdio>
#include <cstring>

namespace hgnfs::loader {

IndexData load_binary(const char* filepath) {
    IndexData d;
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "ERROR: cannot open %s\n", filepath);
        return d;
    }

    // Header: dim, n_chunks, pca_rows, pca_cols
    int dim, n_chunks, pca_rows, pca_cols;
    fread(&dim, sizeof(int), 1, f);
    fread(&n_chunks, sizeof(int), 1, f);
    fread(&pca_rows, sizeof(int), 1, f);
    fread(&pca_cols, sizeof(int), 1, f);
    d.dim = dim;
    d.pca_target_dim = pca_rows;
    d.pca_input_dim = pca_cols;

    // PCA mean: pca_cols floats
    d.pca_mean.resize(pca_cols);
    fread(d.pca_mean.data(), sizeof(float), pca_cols, f);

    // PCA components: pca_rows × pca_cols floats (row-major)
    d.pca_components.resize(pca_rows * pca_cols);
    fread(d.pca_components.data(), sizeof(float), pca_rows * pca_cols, f);

    // Z matrix: n_chunks × dim floats
    d.Z.resize(n_chunks * dim);
    fread(d.Z.data(), sizeof(float), n_chunks * dim, f);

    // Paths
    int n_paths;
    fread(&n_paths, sizeof(int), 1, f);
    d.paths.resize(n_paths);
    for (int i = 0; i < n_paths; ++i) {
        int len;
        fread(&len, sizeof(int), 1, f);
        std::string s(len, '\0');
        fread(&s[0], 1, len, f);
        d.paths[i] = s;
    }

    // Per-chunk: chunk_idx, path_id
    d.metas.resize(n_chunks);
    for (int i = 0; i < n_chunks; ++i) {
        int chunk_idx, path_id;
        fread(&chunk_idx, sizeof(int), 1, f);
        fread(&path_id, sizeof(int), 1, f);
        d.metas[i].chunk_idx = chunk_idx;
        d.metas[i].path = (path_id >= 0 && path_id < n_paths) ? d.paths[path_id] : "";
    }

    fclose(f);
    return d;
}

}  // namespace hgnfs::loader
