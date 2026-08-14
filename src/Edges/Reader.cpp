#include "Reader.h"
#include "Edges/Edges.h"
#include "Edges/Encoding.h"
#include "ObjectParsing/ObjectParsing.h"
#include <absl/log/log.h>
#include <boost/core/span.hpp>
#include <cstdint>
#include <iostream>
#include <optional>
#include <streamvbyte.h>
#include <streamvbytedelta.h>
#include <string.h>
#include <zstd.h>

using namespace bracelet;
using namespace object_parsing;

namespace bracelet::edges {
// This struct exists to cache allocations.
struct GraphDebugDataParser {
  // for<'a> fn(&'a mut self, ...) -> GraphDebugData<'a>
  Result<GraphDebugData>
  parse_debug_data(Object &target, encoding::GraphHeader &header,
                   // No longer allow null debug addrs,
                   // We require the strtable at a minimum.
                   Address debug_data_address) {
    assert(debug_data_address);

    compressed_debug_data.resize(header.debug_data_compressed_size);
    BRACELET_TRY_CONTEXT(
        target.copyData(boost::span(reinterpret_cast<uint8_t *>(
                                        compressed_debug_data.data()),
                                    compressed_debug_data.size()),
                        debug_data_address),
        "Reading debug data at address %x", debug_data_address);
    auto debug_data_size = ZSTD_getFrameContentSize(
        compressed_debug_data.data(), compressed_debug_data.size());
    BRACELET_ENSURE(debug_data_size != ZSTD_CONTENTSIZE_ERROR &&
                      debug_data_size != ZSTD_CONTENTSIZE_UNKNOWN,
                  "Unable to get zstd size for debug data");
    decompressed_debug_data.resize(debug_data_size + STREAMVBYTE_PADDING);
    auto zstd_result = ZSTD_decompress(
        decompressed_debug_data.data(), decompressed_debug_data.size(),
        compressed_debug_data.data(), compressed_debug_data.size());
    BRACELET_ENSURE(!ZSTD_isError(zstd_result),
                  "ZSTD decompression failed, debug data %s, %d",
                  ZSTD_getErrorName(zstd_result), compressed_debug_data.size());
    assert(zstd_result == debug_data_size); // zstd checks this
    std::string_view full_decompressed_debug_data(decompressed_debug_data);
    BRACELET_ENSURE(header.string_blob_length <=
                      full_decompressed_debug_data.size(),
                  "string blob length too large");
    std::string_view string_blob =
        full_decompressed_debug_data.substr(0, header.string_blob_length);

    std::optional<boost::span<uint32_t>> string_indices_ref = std::nullopt;
    if (header.has_debug_locals) {
      std::string_view encoded_local_indices =
          full_decompressed_debug_data.substr(header.string_blob_length);
      BRACELET_ENSURE(
          streamvbyte_validate_stream(
              reinterpret_cast<const uint8_t *>(encoded_local_indices.data()),
              encoded_local_indices.size() - STREAMVBYTE_PADDING,
              header.total_num_locals),
          "Failed to validate debug data local index stream (%d locals encoded "
          "into %d bytes)",
          header.total_num_locals,
          encoded_local_indices.size() - STREAMVBYTE_PADDING);
      local_string_indices.resize(header.total_num_locals);
      streamvbyte_delta_decode(
          reinterpret_cast<const uint8_t *>(encoded_local_indices.data()),
          local_string_indices.data(), local_string_indices.size(), 0);
      string_indices_ref = local_string_indices;
    }

    string_offsets.clear();
    if (!string_blob.empty()) {
      uint32_t offset = 0;
      while (offset < string_blob.size()) {
        string_offsets.push_back(offset);
        auto next = string_blob.find('\0', offset);
        BRACELET_ENSURE(next != std::string_view::npos,
                      "string_blob doesn't end with '\0'");
        offset = next + 1;
      }
      BRACELET_ENSURE(string_blob.back() == '\0',
                    "string_blob doesn't end with '\0'");
    }
    return edges::GraphDebugData(string_blob, string_indices_ref,
                                 string_offsets);
  }

  // Split the GraphDebugData view into locals at pos. Return these two halves.
  // The first half will have locals [0, pos). The next half will have locals
  // [pos, ...), but will map them as [0, ...)
  static Result<std::pair<GraphDebugData, GraphDebugData>>
  debug_data_split_at(GraphDebugData gdd, size_t pos) {
    BRACELET_ENSURE(pos <= gdd.local_indices->size(),
                  "Local index %v is out of range", pos);
    return std::make_pair(
        GraphDebugData(gdd.string_blob, gdd.local_indices->subspan(0, pos),
                       gdd.string_offsets),
        GraphDebugData(gdd.string_blob, gdd.local_indices->subspan(pos),
                       gdd.string_offsets));
  }

private:
  std::string decompressed_debug_data;
  std::string compressed_debug_data;
  std::vector<uint32_t> local_string_indices;
  std::vector<uint32_t> string_offsets;
};
} // namespace bracelet::edges

namespace {
// Parse out the edges for a single object file.
struct Frame {
  edges::encoding::GraphHeader header;
  boost::span<uint32_t> function_data;
  boost::span<Address> symbol_addresses;
  edges::GraphDebugData debug_data;

  Result<edges::Node> decode_node(uint32_t number, uint64_t function_address) {
    if (number & 1)
      return edges::Node::symbol(BRACELET_TRY(symbol_address(number >> 1)));
    else
      return edges::Node::local(function_address, number >> 1);
  }
  Result<Address> symbol_address(size_t idx) const {
    BRACELET_ENSURE(idx < symbol_addresses.size(),
                  "symbol address is out of bounds");
    return symbol_addresses[idx];
  }
  Result<boost::span<uint32_t>> next_words(size_t count) {
    BRACELET_ENSURE(function_data.size() >= count,
                  "unexpected end of function data. Expected at least %v more",
                  count);
    auto out = function_data.subspan(0, count);
    function_data = function_data.subspan(count);
    return out;
  }
  Result<uint32_t> next_word() {
    auto out = BRACELET_TRY(next_words(1));
    return out.front();
  }

  // Increments locals if locals are present
  Result<edges::GraphDebugData> next_locals(uint32_t num_locals) {
    if (!debug_data.has_locals())
      return debug_data;
    auto [out, state] =
        BRACELET_TRY(edges::GraphDebugDataParser::debug_data_split_at(
            debug_data, num_locals));
    debug_data = state;
    return out;
  }
};

// This is stateful to cache allocations.
//
// It decodes graph section data into Frame<DL>s. These are then passed to the
// callback.
struct FrameDecoder {
  explicit FrameDecoder(Object &target) : target(target) {}

  template <typename Cb>
  Result<void> runOnSection(const Section &section, Cb cb) {
    full_section_data.resize(section.size);
    section_start = section.start;
    section_data_cursor = 0;
    BRACELET_TRY_CONTEXT(
        target.copyData(
            boost::span(reinterpret_cast<uint8_t *>(full_section_data.data()),
                        full_section_data.size()),
            section_start),
        "Reading section %s", section.name);
    std::string_view graph_magic_number_bytes = std::string_view(
        reinterpret_cast<const char *>(&edges::encoding::GRAPH_MAGIC_NUMBER),
        sizeof(edges::encoding::GRAPH_MAGIC_NUMBER));
    while (section_data_cursor < full_section_data.size()) {
      auto pos =
          full_section_data.find(graph_magic_number_bytes, section_data_cursor);
      if (pos == std::string_view::npos) {
        pos = full_section_data.size();
        break;
      }
      if (!std::all_of(full_section_data.begin() + section_data_cursor,
                       full_section_data.begin() + pos,
                       [](const char byte) { return byte == 0; })) {
        LOG(WARNING) << "Skipping non-zero graph_data bytes";
      }
      section_data_cursor = pos;
      if (section_data_cursor < full_section_data.size()) {
        std::string_view section_data =
            std::string_view(full_section_data).substr(section_data_cursor);
        assert(section_data.size() >= graph_magic_number_bytes.size());
        assert(section_data.substr(0, graph_magic_number_bytes.size()) ==
               graph_magic_number_bytes);
        Frame frame = BRACELET_TRY(consumeOneGraph());
        BRACELET_TRY(cb(frame));
      }
    }
    return ok();
  }

private:
  std::string full_section_data;
  size_t section_data_cursor;
  Address section_start;
  std::vector<Address> symbol_addresses;
  Object &target;
  std::string function_data_decompressed;
  std::vector<uint32_t> function_data;
  edges::GraphDebugDataParser graph_debug_data_parser;

  Result<std::string_view> consumeBytes(size_t num_bytes,
                                        std::string_view what) {
    BRACELET_ENSURE(section_data_cursor + num_bytes <= full_section_data.size(),
                  "Buffer has only %v bytes. %v expected. (Reading %v)",
                  full_section_data.size() - section_data_cursor, num_bytes,
                  what);
    auto out = std::string_view(full_section_data)
                   .substr(section_data_cursor, num_bytes);
    section_data_cursor += num_bytes;
    return out;
  }
  template <typename T> Result<T> consumeMemcpy(std::string_view what) {
    T t;
    auto bytes = BRACELET_TRY(consumeBytes(sizeof(t), what));
    memcpy(&t, bytes.data(), sizeof(t));
    return t;
  }
  Result<void> consumeAddresses(boost::span<Address> dst,
                                std::string_view what) {
    auto size_bytes = dst.size() * target.pointerSize();
    auto addr = section_start + section_data_cursor;
    // use consumeBytes() to do error-checking and advance the cursor.
    BRACELET_TRY(consumeBytes(size_bytes, what));
    BRACELET_TRY_CONTEXT(target.resolvePointers(dst, addr),
                       "Reading addresses for %s", what);
    return bracelet::ok();
  }
  // This result is only vaild until the next call to consumeOneGraph
  Result<Frame> consumeOneGraph() {
    auto header =
        BRACELET_TRY(consumeMemcpy<edges::encoding::GraphHeader>("graph header"));
    assert(header.magic_number == edges::encoding::GRAPH_MAGIC_NUMBER);
    Address debug_data_address;
    BRACELET_TRY(consumeAddresses(boost::span(&debug_data_address, 1),
                                "debug data address"));
    symbol_addresses.resize(header.num_symbols);
    BRACELET_TRY(consumeAddresses(symbol_addresses, "symbol addresses"));
    auto compressed_function_data = BRACELET_TRY(consumeBytes(
        header.function_data_compressed_size, "compressed function data"));
    auto function_data_size = ZSTD_getFrameContentSize(
        compressed_function_data.data(), compressed_function_data.size());
    BRACELET_ENSURE(function_data_size != ZSTD_CONTENTSIZE_ERROR,
                  "Unable to get zstd size for function data");
    BRACELET_ENSURE(function_data_size != ZSTD_CONTENTSIZE_UNKNOWN,
                  "Missing zstd size for function data");
    function_data_decompressed.resize(function_data_size + STREAMVBYTE_PADDING);
    auto zstd_result = ZSTD_decompress(
        function_data_decompressed.data(), function_data_size,
        compressed_function_data.data(), compressed_function_data.size());
    BRACELET_ENSURE(!ZSTD_isError(zstd_result),
                  "ZSTD decompression failed, function data");
    assert(zstd_result == function_data_size); // zstd checks this
    function_data.resize(header.function_array_length);
    BRACELET_ENSURE(streamvbyte_validate_stream(
                      reinterpret_cast<const uint8_t *>(
                          function_data_decompressed.data()),
                      function_data_size, header.function_array_length),
                  "Invalid streamvbyte function data");
    streamvbyte_decode(
        reinterpret_cast<const uint8_t *>(function_data_decompressed.data()),
        function_data.data(), function_data.size());

    return Frame{.header = header,
                 .function_data = function_data,
                 .symbol_addresses = boost::span(symbol_addresses),
                 .debug_data =
                     BRACELET_TRY(graph_debug_data_parser.parse_debug_data(
                         target, header, debug_data_address))};
  }

  Result<Address> getSymbol(uint32_t idx) {
    BRACELET_ENSURE(idx < symbol_addresses.size(),
                  "Symbol index %v is out of bounds", idx);
    return symbol_addresses[idx];
  }
};

struct ConcreteFunctionInfo : public edges::FunctionInfo {
  ConcreteFunctionInfo(const bracelet::edges::GraphDebugData &debug_data)
      : FunctionInfo(debug_data) {}

  virtual Result<void>
  visitEdges(std::function<Result<void>(const edges::Assign &)> cb) override {
    return visitEdgesInner(cb);
  }
  virtual Result<void>
  visitEdges(std::function<Result<void>(const edges::Load &)> cb) override {
    return visitEdgesInner(cb);
  }
  virtual Result<void>
  visitEdges(std::function<Result<void>(const edges::Store &)> cb) override {
    return visitEdgesInner(cb);
  }
  virtual Result<void>
  visitEdges(std::function<Result<void>(const edges::Call &)> cb) override {
    return visitEdgesInner(cb);
  }
  virtual Result<void>
  visitEdges(std::function<Result<void>(const edges::Return &)> cb) override {
    return visitEdgesInner(cb);
  }
  virtual Result<void>
  visitEdges(std::function<Result<void>(const edges::ArgumentDefinition &)> cb)
      override {
    return visitEdgesInner(cb);
  }
  virtual Result<void> visitEdges(
      std::function<Result<void>(const edges::ArgumentSupply &)> cb) override {
    return visitEdgesInner(cb);
  }
  virtual Result<void>
  visitEdges(std::function<Result<void>(const edges::DlsymPagePointer &)> cb)
      override {
    return visitEdgesInner(cb);
  }

  template <typename Edge>
  Result<void> visitEdgesInner(std::function<Result<void>(const Edge &)> &cb) {
    auto edge_data = std::get<EdgeData<Edge>>(this->edge_data).data;
    uint32_t prev = 0;
    for (size_t i = 0; i < edge_data.size(); i += Edge::NUM_COMPONENTS) {
      uint32_t to_delta = edges::encoding::zigzag_decode(edge_data[i]);
      uint32_t to_number = prev + to_delta;
      prev = to_number;
      uint32_t from_delta = edges::encoding::zigzag_decode(edge_data[i + 1]);
      uint32_t from_number = prev + from_delta;
      prev = from_number;
      auto to = BRACELET_TRY(frame->decode_node(to_number, function.symbol()));
      auto from =
          BRACELET_TRY(frame->decode_node(from_number, function.symbol()));
      if constexpr (Edge::NUM_COMPONENTS == 2)
        BRACELET_TRY(cb(Edge(to, from)));
      else if constexpr (Edge::NUM_COMPONENTS == 3)
        BRACELET_TRY(cb(Edge(to, from, edge_data[i + 2])));
      else
        assert(0);
    }
    return ok();
  }

  template <typename E> struct EdgeData {
    using Edge = E;
    boost::span<uint32_t> data;
  };
  edges::EdgeTuple<EdgeData> edge_data;
  Frame *frame;
};

} // namespace

Result<const char *> edges::GraphDebugData::string(size_t string_idx) const {
  BRACELET_ENSURE(string_idx < string_offsets.size(),
                "string index %v is out of bounds", string_idx);
  return string_blob.data() + string_offsets[string_idx];
}

Result<const char *> edges::GraphDebugData::local_name(size_t idx) const {
  BRACELET_ENSURE(idx < local_indices->size(), "local idx %v is out of bounds",
                idx);
  auto string_idx = (*local_indices)[idx];
  return this->string(string_idx);
}

Result<void>
edges::readEdges(bracelet::object_parsing::Object &target,
                 std::function<Result<void>(edges::FunctionInfo &)> callback) {
  bool is_apple = target.isApple();
  edges::encoding::SectionNames section_names(is_apple);
  std::string_view graph_section_name(section_names.graph);
  FrameDecoder reader(target);
  BRACELET_TRY(target.visitSections([&](const Section &section) -> Result<void> {
    if (section.name != graph_section_name)
      return bracelet::ok();
    BRACELET_TRY(reader.runOnSection(section, [&](Frame frame) -> Result<void> {
      for (uint32_t function_idx = 0; function_idx < frame.header.num_functions;
           function_idx++) {
        auto function_symbol_index = BRACELET_TRY(frame.next_word());
        auto component_string_index = BRACELET_TRY(frame.next_word());
        auto version_string_index = BRACELET_TRY(frame.next_word());
        auto function_address =
            BRACELET_TRY(frame.symbol_address(function_symbol_index));
        auto num_locals = BRACELET_TRY(frame.next_word());
        auto num_allocas = BRACELET_TRY(frame.next_word());
        BRACELET_ENSURE(num_allocas <= num_locals,
                      "num_allocas (%v) <= num_locals (%v)", num_allocas,
                      num_locals);
        auto debug_data = BRACELET_TRY(frame.next_locals(num_locals));
        ConcreteFunctionInfo cfi(debug_data);
        cfi.num_allocas = num_allocas;
        cfi.num_locals = num_locals;

        cfi.function = edges::Node::symbol(function_address);
        cfi.frame = &frame;

        std::optional<bracelet::edges::encoding::SBOMInformation> sbomInfo;

        std::string sComp =
            BRACELET_TRY(cfi.debug_data.string(component_string_index));
        std::string vComp =
            BRACELET_TRY(cfi.debug_data.string(version_string_index));
        if (!sComp.empty() && !vComp.empty()) {
          sbomInfo = {sComp, vComp};
        }

        cfi.sbom_info = sbomInfo;

        BRACELET_TRY(
            edges::visitTuple(cfi.edge_data, [&](auto &dst) -> Result<void> {
              using Edge = typename std::decay_t<decltype(dst)>::Edge;
              size_t edge_count = BRACELET_TRY(frame.next_word());
              dst.data = BRACELET_TRY(
                  frame.next_words(Edge::NUM_COMPONENTS * edge_count));
              return ok();
            }));
        BRACELET_TRY(callback(cfi));
      }
      return ok();
    }));
    return ok();
  }));
  return ok();
}
