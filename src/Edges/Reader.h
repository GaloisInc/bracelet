#pragma once

#include "Edges.h"
#include "Edges/Encoding.h"
#include "ObjectParsing/ObjectParsing.h"
#include "Result/Result.h"
#include <boost/core/span.hpp>
#include <boost/operators.hpp>
#include <functional>
#include <optional>
#include <type_traits>

namespace bracelet {
namespace edges {

struct GraphDebugDataParser;
struct GraphDebugData {
  // The debug name of local idx for this function.
  Result<const char *> string(size_t idx) const;
  Result<const char *> local_name(size_t idx) const;
  bool has_locals() const { return this->local_indices.has_value(); }
  size_t num_locals() const { return local_indices->size(); }

private:
  friend struct GraphDebugDataParser;
  GraphDebugData(std::string_view string_blob,
                 std::optional<boost::span<uint32_t>> local_indices,
                 boost::span<uint32_t> string_offsets)
      : string_blob(string_blob), local_indices(local_indices),
        string_offsets(string_offsets) {}

  std::string_view string_blob;
  std::optional<boost::span<uint32_t>> local_indices;
  boost::span<uint32_t> string_offsets;
};

struct FunctionInfo {
  edges::Node function;
  // If local_idx < num_allocas, then local_idx is an alloca
  unsigned num_allocas;
  unsigned num_locals;
  const GraphDebugData &debug_data;
  std::optional<bracelet::edges::encoding::SBOMInformation> sbom_info;
  FunctionInfo(const GraphDebugData &debug_data) : debug_data(debug_data) {}
  virtual ~FunctionInfo() {}

  // These functions will call the callback argument for each edge of the given
  // type.
  virtual Result<void>
      visitEdges(std::function<Result<void>(const Assign &)>) = 0;
  virtual Result<void>
      visitEdges(std::function<Result<void>(const Load &)>) = 0;
  virtual Result<void>
      visitEdges(std::function<Result<void>(const Store &)>) = 0;
  virtual Result<void>
      visitEdges(std::function<Result<void>(const Call &)>) = 0;
  virtual Result<void>
      visitEdges(std::function<Result<void>(const Return &)>) = 0;
  virtual Result<void>
      visitEdges(std::function<Result<void>(const ArgumentDefinition &)>) = 0;
  virtual Result<void>
      visitEdges(std::function<Result<void>(const ArgumentSupply &)>) = 0;
  virtual Result<void>
      visitEdges(std::function<Result<void>(const DlsymPagePointer &)>) = 0;

  // Call cb for all edges in the function.
  template <typename Cb> Result<void> visitAllEdges(Cb cb) {
    edges::EdgeTuple<VisitHack> visit_hack;
    return edges::visitTuple(visit_hack, [&](auto edge_data) {
      using Edge = typename std::remove_reference_t<decltype(edge_data)>::Edge;
      auto cb_wrapper = [&](const Edge &e) -> Result<void> { return cb(e); };
      std::function<Result<void>(const Edge &)> cb_f(cb_wrapper);
      return visitEdges(cb_f);
    });
  }

private:
  template <typename E> struct VisitHack {
    using Edge = E;
  };
};

// Read the graph from target and invoke callback for each function.
//
// callback: for<'a> FnMut(&'a FunctionInfo) -> Result<()>
Result<void> readEdges(bracelet::object_parsing::Object &target,
                       std::function<Result<void>(FunctionInfo &)> callback);

} // namespace edges
} // namespace bracelet
