#pragma once
#include <absl/container/flat_hash_map.h>
#include <array>
#include <cstdint>
#include <llvm/IR/Comdat.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/Module.h>
#include <tuple>
#include <type_traits>
#include <vector>

#include "Edges.h"
#include "Encoding.h"

namespace bracelet {
namespace edges {

// Add Graph data to an LLVM module.
//
// To add data for an individual function, construct a FunctionWriter.
class GraphWriter {
public:
  using StringIndex = uint32_t;

  // The GraphWriter will live inside the given Comdat (or no Comdat, if null is
  // specified).
  //
  // A graph for function f should live inside whatever Comdat f lives inside of
  explicit GraphWriter(llvm::Comdat *comdat, bool record_debug_data);
  GraphWriter(
      llvm::Comdat *OutputComdat,
      const std::optional<encoding::SBOMInformation> &SbomInfo,
      const std::unordered_map<std::string, encoding::SBOMInformation> &SbomMap,
      bool record_debug_data);

  Node symbol(llvm::GlobalValue &);
  void writeTo(llvm::Module &) const;
  std::optional<encoding::SBOMInformation>
  getSbomInfoOfFunction(const llvm::Function *func) const;

private:
  friend class FunctionWriter;
  StringIndex string(llvm::StringRef);

  llvm::StringMap<StringIndex> StringBlobIndices;
  std::string StringBlob;
  absl::flat_hash_map<llvm::GlobalValue *, uint32_t> Symbols;
  std::vector<llvm::Constant *> symbols_in_order;
  std::vector<StringIndex> local_names;
  std::vector<uint32_t> function_data;
  std::optional<encoding::SBOMInformation> SbomInfo;
  std::unordered_map<std::string, encoding::SBOMInformation> FileToSbomInfo;
  bool record_debug_data;

  uint32_t num_functions = 0;
  llvm::Comdat *OutputComdat;

  bool has_active_function_writer = false;
};

// Add graph data for a particular function
//
// Only one FunctionWriter can be active at once.
class FunctionWriter {
public:
  FunctionWriter(GraphWriter &, llvm::Function &,
                 // If true will record local names
                 bool record_debug_data);
  ~FunctionWriter();
  Node freshLocal(llvm::StringRef Name);
  Node thisFunction() { return Node::symbol(ThisSymbolIdx); }
  template <typename E> void addEdge(E e) {
    using EdgeType = std::decay_t<E>;
    EdgeStorage<EdgeType> &edges = std::get<EdgeStorage<EdgeType>>(m_edges);
    std::array<uint32_t, EdgeType::NUM_COMPONENTS> arr;
    arr[0] = encode_node(std::get<0>(e));
    arr[1] = encode_node(std::get<1>(e));
    if constexpr (EdgeType::NUM_COMPONENTS == 3)
      arr[2] = std::get<2>(e);
    edges.data.push_back(arr);
  }
  // The first NumAllocas locals will be tagged as allocas.
  void finish(uint32_t NumAllocas);

  GraphWriter &getGraphWriter() const { return GW; }

private:
  uint32_t encode_node(Node n) {
    if (auto Local = n.local_idx()) {
      assert(n.symbol() == ThisSymbolIdx);
      return static_cast<uint32_t>(*Local) << 1;
    }
    return (n.symbol() << 1) | 1;
  }

  uint64_t ThisSymbolIdx;
  GraphWriter::StringIndex SbomComponentIdx;
  GraphWriter::StringIndex SbomVersionIdx;
  bool Written;
  GraphWriter &GW;
  bool record_debug_data;
  uint32_t num_locals = 0;

  template <typename E> struct EdgeStorage {
    using EdgeType = E;
    std::vector<std::array<uint32_t, E::NUM_COMPONENTS>> data;
  };
  EdgeTuple<EdgeStorage> m_edges;
};
} // namespace edges
} // namespace bracelet
