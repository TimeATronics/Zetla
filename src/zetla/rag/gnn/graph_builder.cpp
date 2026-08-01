#include "graph_builder.hpp"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace hgnfs::graph {

FsGraph build_filesystem_graph(const std::string& root_dir,
                                const std::vector<std::string>& exclude_dirs) {
    FsGraph g;
    std::vector<std::string> exclude(exclude_dirs.begin(), exclude_dirs.end());
    exclude.insert(exclude.end(), {".git", ".venv", "__pycache__", ".pytest_cache", "node_modules"});

    if (!fs::exists(root_dir) || !fs::is_directory(root_dir)) return g;

    // Root node
    fs::path root_path = fs::absolute(root_dir);
    g.nodes.push_back({0, NodeKind::DIRECTORY, root_path.string(), root_path.filename().string()});
    g.path_to_id[root_path.string()] = 0;
    int next_id = 1;

    for (auto it = fs::recursive_directory_iterator(root_path, fs::directory_options::skip_permission_denied);
         it != fs::recursive_directory_iterator(); ++it) {
        const auto& entry = *it;
        std::string name = entry.path().filename().string();
        std::string parent = entry.path().parent_path().string();

        // Skip excluded directories
        if (entry.is_directory() &&
            std::find(exclude.begin(), exclude.end(), name) != exclude.end()) {
            it.disable_recursion_pending();
            continue;
        }

        auto parent_it = g.path_to_id.find(parent);
        if (parent_it == g.path_to_id.end()) continue;
        int parent_id = parent_it->second;

        int id = next_id++;
        if (entry.is_directory()) {
            g.nodes.push_back({id, NodeKind::DIRECTORY, entry.path().string(), name});
        } else if (entry.is_regular_file()) {
            g.nodes.push_back({id, NodeKind::FILE, entry.path().string(), name});
        } else {
            next_id--; continue;
        }
        g.path_to_id[entry.path().string()] = id;
        g.edges.push_back({parent_id, id});
        g.edges.push_back({id, parent_id});  // undirected
    }
    return g;
}

std::vector<std::vector<int>> to_adjacency_lists(const FsGraph& g) {
    int N = static_cast<int>(g.nodes.size());
    std::vector<std::vector<int>> adj(N);
    for (auto& e : g.edges) {
        if (e.src >= 0 && e.src < N && e.dst >= 0 && e.dst < N)
            adj[e.src].push_back(e.dst);
    }
    return adj;
}

}  // namespace hgnfs::graph
