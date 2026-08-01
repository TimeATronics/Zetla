#pragma once
#include "../core/types.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace hgnfs::graph {

enum class NodeKind { DIRECTORY, FILE };
struct FsNode { int id; NodeKind kind; std::string path, name; };
struct FsEdge { int src, dst; };
struct FsGraph {
    std::vector<FsNode> nodes;
    std::vector<FsEdge> edges;
    std::unordered_map<std::string, int> path_to_id;
};

/// Walk root_dir and return a graph of directories and files.
FsGraph build_filesystem_graph(const std::string& root_dir,
                                const std::vector<std::string>& exclude_dirs = {});

/// Convert graph edges to adjacency lists (int indices).
std::vector<std::vector<int>> to_adjacency_lists(const FsGraph& g);

}  // namespace hgnfs::graph
