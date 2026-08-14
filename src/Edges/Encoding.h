#pragma once

#include <array>
#include <stdint.h>
#include <string>
#include <string_view>

namespace bracelet {
namespace edges {
namespace encoding {

const std::string BraceletCallsiteDwarfLabelPrefix = "BRACELET_LAB_";

struct SBOMInformation {
  std::string ComponentName;
  std::string Version;
};

struct SectionNames {
  explicit SectionNames(bool is_apple) {
    if (is_apple) {
      graph = "__DATA,__GR_graph_edges";
      debug = "__DATA,__GR_graph_debug";
      trace_site = "__DATA,__GR_trace_site";
    } else {
      graph = "GR_graph_edges";
      debug = "GR_graph_debug";
      trace_site = "GR_trace_site";
    }
  }

  std::string_view graph;
  std::string_view debug;
  std::string_view trace_site;
};

constexpr std::array<uint32_t, 2> GRAPH_MAGIC_NUMBER = {0x3ac73b4eU,
                                                        0x445ecf19};
struct GraphHeader {
  std::array<uint32_t, 2> magic_number;
  uint32_t function_data_compressed_size, debug_data_compressed_size,
      num_symbols, string_blob_length, has_debug_locals, total_num_locals,
      num_functions;
  uint32_t function_array_length;
};

static_assert(sizeof(GraphHeader) == 40, "Graph header should be exact");

/*
struct GraphData {
  GraphHeader H;
  // This points at a ZSTD-compressed blob
  DebugData* DD;
  void* Symbols[NumSymbols];
  FunctionData[NumFunctions];
};

FunctionData is a stream of uint32_ts which have been compressed with
streamvbyte (NOT streamvbyte's delta encoding). That streamvbyte output is then
compressed with zstd.

FunctionData = zstd_compress(streamvbyte_encode(array_of_integers));

// The contents of the FunctionData array is a stream, for each function:
template<typename EdgeKind>
struct Edges {
  uint32_t num_edges;
  struct {
    // For each edge kind, we do a fresh zig-zag delta encoding, and store those
    // encoded deltas in the to/from fields. A _single_ running delta is shared
    // among _both_ to and from.
    DeltaEncodedZigZag to, from;
    if(EdgeKind == IndexedEdge) uint32_t index;
  } [num_edges];
};
struct FunctionData {
  uint32_t symbol_index; // The symbol index of this function
  uint32_t num_locals;
  // Alloca locals come first in the ordering. Any locals with an index under
  // this threshold are allocas.
  uint32_t num_allocas;
  Edges<SingletonEdge> assign;
  // ...
  Edges<IndexedEdge> call;
  // ...
};
struct DebugData {
  // Each string in the blob is null-terminated. And a StringIndex refers to
  // the index of the String in this blob. (Not an offset into this blob.)
  char string_blob[StringBlobLength];
  // This is a streambyte delta-encoded array.
  StringIndex local_names[TotalNumLocals];
};
*/

// Based on
// https://lemire.me/blog/2022/11/25/making-all-your-integers-positive-with-zigzag-encoding/
inline uint32_t zigzag_encode(int32_t x) {
  return (2 * x) ^ (x >> (sizeof(x) * 8 - 1));
}
inline int32_t zigzag_decode(uint32_t x) { return (x >> 1) ^ (-(x & 1)); }

} // namespace encoding
} // namespace edges
} // namespace bracelet
