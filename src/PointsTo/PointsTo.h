#pragma once

#include "Edges/Edges.h"
#include "ObjectParsing/ObjectParsing.h"
#include "Result/Result.h"
#include "absl/container/flat_hash_set.h"
#include <boost/graph/compressed_sparse_row_graph.hpp>
#include <filesystem>

namespace bracelet {
namespace points_to {

struct SVFInstall {
  std::filesystem::path SVF_DIR;
  std::optional<std::filesystem::path> SVF_CLANG;
  std::optional<std::filesystem::path> SVF_LLVM;
};

using PointsToGraph =
    boost::compressed_sparse_row_graph<boost::directedS, edges::Node>;
using PointsToEdges = absl::flat_hash_set<std::pair<edges::Node, edges::Node>>;

// Invoke SVF as a subprocess (using docker or podman if it's not installed
// locally) to compute the static points-to graph.
Result<PointsToEdges>
computePointsTo(object_parsing::Object &obj, bool conservative_mode,
                bool save_pts, const SVFInstall &install_paths,
                // If specified, use tmp as the temporary directory
                const std::filesystem::path *tmp = nullptr);

// Invoke SVF as a subprocess (using docker or podman if it's not installed
// locally) to compute the static points-to graph, but don't load into memory
Result<void> runPointsTo(object_parsing::Object &obj, bool conservative_mode,
                         bool save_pts, const SVFInstall &install_paths,
                         // If specified, use tmp as the temporary directory
                         const std::filesystem::path *tmp = nullptr);

// Compare a static points-to graph against dynamic traces from a program run.
Result<void> checkPointsToAgainstTrace(object_parsing::CoredumpObject &obj,
                                       PointsToEdges &static_edges,
                                       const std::filesystem::path &traces_dir);

} // namespace points_to
} // namespace bracelet
