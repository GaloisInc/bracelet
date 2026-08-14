#pragma once

#include "Edges/Edges.h"
#include "Result/Result.h"
#include "absl/container/flat_hash_map.h"
#include "boost/core/span.hpp"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include <boost/container/small_vector.hpp>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <llvm/Object/ObjectFile.h>
#include <map>
#include <memory>
#include <optional>
#include <string_view>

namespace lldb {
class SBTarget;
}

namespace bracelet {
namespace object_parsing {
namespace debug_info {
struct AddressRange {
  uint64_t start, end;
};
struct SourceLocation {
  std::string_view file;
  uint32_t line;
  uint32_t col;

  bool operator==(const SourceLocation &sl) const {
    return file == sl.file && line == sl.line && col == sl.col;
  }
  bool operator!=(const SourceLocation &sl) const { return !(*this == sl); }

  template <typename H> friend H AbslHashValue(H h, const SourceLocation &sl) {
    return H::combine(std::move(h), sl.file, sl.line, sl.col);
  }
};
// A mapping from source locations to a list of AddressRanges of instructions
// for that source.
using SourceLocationMap =
    absl::flat_hash_map<SourceLocation,
                        boost::container::small_vector<AddressRange, 4>>;
}; // namespace debug_info

// For coredumps, these are load addresses.
// For static objects, these aren't load addresses or file addresses. They are
// (section_id, offset) pairs.
using Address = uint64_t;

struct Section {
  std::string_view name;
  Address start;
  uint64_t size;
};

struct CallsiteEdge {
  uint64_t callsiteAddr;
  edges::Node nodeID;
};

struct InlineEdge {
  edges::Node inlinerID;
  std::string_view inlinedName;
};

struct Object {
  using CUCallsites = std::multimap<uint64_t, CallsiteEdge>;
  using AddressTranslation = std::function<Result<Address>(Address)>;
  using CUInlineEdges = std::multimap<uint64_t, InlineEdge>;
  virtual ~Object() {}
  // TODO: implement this based on the object file
  virtual size_t pointerSize() const { return 8; }
  virtual bool isApple() = 0;
  // The Data& shouldn't be used after the callback returns.
  virtual Result<void>
  visitSections(std::function<Result<void>(const Section &)> callback) = 0;

  virtual bool SupportsFastAddrNodeMap() { return false; }

  virtual Result<void> visitDwarfContexts(
      std::function<Result<void>(std::unique_ptr<llvm::DWARFContext>,
                                 AddressTranslation)>) = 0;

  Result<void>
      visitCUAddressMaps(std::function<Result<void>(const CUCallsites &)>);

  Result<void>
      visitCUInlineEdges(std::function<Result<void>(const CUInlineEdges &)>);

  virtual Result<const std::string_view> symbolName(Address) = 0;
  virtual Result<void> copyData(boost::span<uint8_t> dst, Address addr) = 0;
  virtual Result<void> resolvePointers(boost::span<Address> dst,
                                       Address addr) = 0;
  virtual Result<std::optional<debug_info::SourceLocationMap>>
  sourceLocationMap() {
    return std::nullopt;
  };
};

struct CoredumpObject : public Object {
  virtual lldb::SBTarget &getTarget() = 0;
};

Result<std::tuple<std::unique_ptr<llvm::object::ObjectFile>,
                  std::unique_ptr<llvm::MemoryBuffer>>>
openLLVMObject(const std::filesystem::path &exe);

Result<std::unique_ptr<Object>> openObject(const std::filesystem::path &exe);
Result<std::unique_ptr<CoredumpObject>>
openCore(const std::filesystem::path &exe, const std::filesystem::path &sysroot,
         const std::filesystem::path &coredump);
} // namespace object_parsing
} // namespace bracelet
