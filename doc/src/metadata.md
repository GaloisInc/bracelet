# Metadata Specification

The BRACELET toolchain embeds metadata that enables post-hoc reachability analysis of symbols with respect to SBOM-like information.

BRACELET metadata is stored in three places, the two sections: `GR_graph_debug` and `GR_graph_edges` and in DWARF sections via DW_TAG_label's of callsites.


## GR_graph_edges

The primary data-structures for BRACELET are stored in the `GR_graph_edges` section.

The rough C types for the graph data stored in this section are below:
```
struct GraphData {
  GraphHeader H;
  // This points at a ZSTD-compressed blob in the debug data section.
  DebugData* DD;
  void* Symbols[num_symbols];
  // Inline z-std compressed
  FunctionData[num_functions];
};


struct GraphHeader {
  std::array<uint32_t, 2> magic_number;
  uint32_t function_data_compressed_size;
  uint32_t debug_data_compressed_size;
  uint32_t num_symbols
  uint32_t string_blob_length;
  uint32_t has_debug_locals;
  uint32_t has_debug_locals;
  uint32_t total_num_locals;
  uint32_t num_functions;
  uint32_t function_array_length;
};
```
The graph data is placed directly in `GR_graph_edges`. Abstractly, this data represents a stream of edges per function.
Importantly, the `Symbols` table associates a virtual address to a symbol index. The `FunctionData` for a given function stores the symbol index of that function. The linker updates the addresses in the symbol table via the relocations for the GR_graph_edges section, allowing BRACELET to find which function address metadata is associated with post-link. 

*IMPORTANT note*: A current limitation of this strategy is that the symbol table will always point to an updated address for a symbol even if the symbol was weak for our given module and not linked. This in practice means, that for weak symbols, metadata for all definitions (even unlinked definitions) are preserved and equivalent. We lose track of which weak symbol is invoked. Currently, based on how `bracelet-edges` works this will result in the last metadata record observed "winning" as it is written last.

`FunctionData` is a stream of `uint32_t` which have been compressed with
[streamvbyte](https://github.com/fast-pack/streamvbyte) (NOT streamvbyte's delta encoding). That streamvbyte output is then
compressed with zstd.

```
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
  uint32_t sbom_component_index; // The index into the debug table of the string representing the CycloneDX component this function came from.
  uint32_t sbom_version_index; // The index into the debug table of the string representing the version of the component this symbol came from. 
  uint32_t num_locals;
  // Alloca locals come first in the ordering. Any locals with an index under
  // this threshold are allocas.
  uint32_t num_allocas;
  Edges<SingletonEdge> assign;
  // ...
  Edges<IndexedEdge> call;
  // ...
};
```

The first `uint32_t` is the index for the function into the symbol table. This index is used by the reader to retrieve the function address of this data.

The second `uint32_t` is the number of locals (and the third is allocas). These sizes are used to find the function data's local names in the debug data (that is string names of local variables stored in the debug_data string table described in [the graph debug data section](#GR_graph_debug)). 

Finally, for each edge type there is a stream of tuples representing edges between nodes. Specifically there are two kinds of edges `Singleton` edges which are of the form (dst, src), and `Indexed` edges which are of the form (dst, src, idx). The src and dst are nodes which identify either a symbol or a local which is part of a symbol (that is a symbol + an index to some local).

### Edge Types

#### Singleton: Assign
Format: (dest, source), represents the assignment of some source node (a local) to the dest node (another local).

#### Singleton: Load
Format: (into, addr), represents a load from addr (a local holding the addr) into the into (a local).

#### Indexed: Call
Format: (callsite, callee, nargs), represents a call from a callsite (a local representing the LLVM value of the call) to the callee (a local or a direct call to a symbol node). nargs is the number of arguments to the call.

#### Singleton: Return
Format: (func, value), represents a call from a function func (symbol) into value (a local representing the return).

#### Indexed: ArgumentDefinition
Format (value, func, arg_no), represents the formal parameter of the function func (a symbol) that is assigned into the value value (a local). The arg_no is the argument number of this local.

#### Indexed: ArgumentSupply
Format (callsite, value, arg_no), represents the actual parameter passed at the callsite (a local representing the call) that is assigned the value value (a local). The arg_no is the argument number of this actual parameter.

#### Singleton: DlsymPagePointer
Format (dlsym_output, page_ptr), used to track the address of page_ptr (symbol) dynamically holding the values returned by this dlsym callsite represented by the local for that callsite (dlsym_output). These edges allow us to (in a snapshot) retrieve the set of addresses the return of dlsym could point to.


## GR_graph_debug

The graph debug section is a zlib compressed null-delimited string table. There are two purposes of this table: 
1. To store the names of locals if debug info is not disabled (`no-bracelet-include-debug-data`)
2. To store the strings for SBOM information: that is the component and version that sourced this symbol (used for the identity of the symbol when analyzing VEX records).

Specifically:
```
struct DebugData {
  // Sequence of null terminated strings
  char* string_blob;
  // Indices of local names for each function in order. FunctionData uses num_locals to find the set of locals for each function by traversing debug data in order.
  Optional<Array<uint32_t>> local_indices;
};
```

The debug data interns strings for all strings used within function data (local names and SBOM information). The indices of each local are stored in order in a streamv encoded array of integers. Each function's locals are stored in a slice `num_locals` large in this array.

*Note*: the value of `has_debug_locals` in the graph header is used to determine if the local_indices field is present.

## DWARF Labels

When constructing a callgraph, BRACELET must relate an address of a call to the node for the callsite in the graph. This relationship allows various pointer analyses to translate from pointer relationships between locals/nodes to actual call targets.

This information is stored as special debug labels attached to the call. Specifically, BRACELET generates a DW_TAG_label with the name "BRACELET_LAB_X" where X is the local index of the call. The parent suprocedure of the DWARF label DIE is used to find the containing function address for the node (func_addr, local). This setup allows BRACELET to generate a table from callsite address to node without generating debug information which is slow to print.

Previously, BRACELET used the source location from debug information (in the debug table) for the callsite to find corresponding addresses to that source location in DWARF. This solution was slow and also selects all addresses associated with an address range rather than the actual address of the call.

## Dlsym tables

As mentioned when describing Dlsym edges, dlsym tables hold the pointers returned by a given dlsym callsite. Each dlsym callsite gets a linked-list of pages containing pointers. 

```
struct DlsymPage {
  // The next page or null, the runtime will allocate more pages as needed
  void* next;
  // the number of pointers on this page
  uint64_t count;
  void* pointers[MAX_POINTERS_PER_PAGE];
}
```

The page contains the set of pointers returned from a given callsite. The BRACELET runtime allocates these pages on the fly during execution.
