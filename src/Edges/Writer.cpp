#include "Writer.h"
#include "Encoding.h"
#include "llvm/Support/raw_ostream.h"

#include <boost/sort/pdqsort/pdqsort.hpp>
#include <limits>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/Support/Debug.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include <streamvbyte.h>
#include <streamvbytedelta.h>
#include <zstd.h>

using namespace llvm;
using namespace bracelet::edges;
using namespace bracelet::edges::encoding;

namespace {
Comdat *requiredComdat(GlobalObject *GO) {
  if (!GO->hasComdat())
    return nullptr;
  // If the global object has a comdat, we need to start making guesses. The
  // semantics of comdat varies but, in the worst case, the linker will choose
  // exactly one comdat section bearing a given key/name.
  //
  // There's no _inherent_ reason why that section key needs to correspond to
  // any symbol name. But, in practice, it matches a C++ symbol name. If that
  // symbol is global, we assume that this global/external symbol is going to be
  // defined in _every_ other comdat section sharing this key. If the symbol
  // isn't external (e.g. for initialization code for templated statics), then
  // any attempts to use this symbol will only work if the linker chooses _the
  // particular comdat instance in this module_. Otherwise, while other comdat
  // sections could have a symbol with a similar name, because it's not
  // exported, we have no way of referring to it from our module.
  if (GO->hasInternalLinkage() || GO->hasPrivateLinkage())
    return GO->getComdat();
  return nullptr;
}

void assertComdat(Comdat *C, Comdat *OutputComdat) {
  if (C == nullptr)
    return;
  if (OutputComdat == nullptr ||
      C->getSelectionKind() != OutputComdat->getSelectionKind() ||
      C->getName() != OutputComdat->getName()) {
    errs() << "Expected comdat ";
    C->print(errs(), true);
    errs() << " but have comdat ";
    if (OutputComdat == nullptr)
      errs() << " nullptr";
    else
      OutputComdat->print(errs(), true);
    errs() << "\n";
    abort();
  }
}

template <typename T> uint32_t as_u32(T t) {
  uint64_t x = t;
  assert(x <= std::numeric_limits<uint32_t>::max());
  return x;
}
} // namespace

GraphWriter::GraphWriter(Comdat *OutputComdat, bool record_debug_data)
    : record_debug_data(record_debug_data), OutputComdat(OutputComdat) {}

GraphWriter::GraphWriter(
    Comdat *OutputComdat, const std::optional<SBOMInformation> &SbomInfo,
    const std::unordered_map<std::string, SBOMInformation> &SbomMap,
    bool record_debug_data)
    : SbomInfo(SbomInfo), FileToSbomInfo(SbomMap),
      record_debug_data(record_debug_data), OutputComdat(OutputComdat) {}

Node GraphWriter::symbol(GlobalValue &GV) {
  GlobalObject *GO;
  if (auto *X = dyn_cast<GlobalObject>(&GV)) {
    GO = X;
  } else {
    auto *GA = cast<GlobalAlias>(&GV);
    GO = GA->getAliaseeObject();
  }
  assertComdat(requiredComdat(GO), OutputComdat);
  auto Iter = Symbols.find(GO);
  if (Iter == Symbols.end()) {
    auto Idx = Symbols.size();
    assert(Idx == symbols_in_order.size());
    Symbols.insert(std::make_pair(GO, Idx));
    symbols_in_order.push_back(GO);
    return Node::symbol(Idx);
  }
  return Node::symbol(Iter->second);
}
GraphWriter::StringIndex GraphWriter::string(StringRef S) {
  auto Iter = StringBlobIndices.find(S);
  if (Iter != StringBlobIndices.end()) {
    return Iter->second;
  }
  auto Idx = StringBlobIndices.size();
  StringBlob += S;
  StringBlob.push_back('\0');
  StringBlobIndices.insert(std::make_pair(S, Idx));
  return Idx;
}

std::optional<SBOMInformation>
GraphWriter::getSbomInfoOfFunction(const llvm::Function *func) const {
  // TODO(Ian): maybe use the checksum here
  if (func && func->getSubprogram() && func->getSubprogram()->getFile()) {
    auto dir = func->getSubprogram()->getFile()->getDirectory();
    auto file = func->getSubprogram()->getFile()->getFilename();

    auto pth = std::filesystem::weakly_canonical(
        std::filesystem::path(dir.str()) / file.str());
    auto pthstr = pth.string();
    auto maybe_sbominfo = this->FileToSbomInfo.find(pthstr);
    if (maybe_sbominfo != this->FileToSbomInfo.end()) {
      return maybe_sbominfo->second;
    }
  }

  return this->SbomInfo;
}

FunctionWriter::FunctionWriter(GraphWriter &GW, Function &F,
                               bool record_debug_data)
    : Written(false), GW(GW), record_debug_data(record_debug_data) {
  assert(!GW.has_active_function_writer);
  GW.has_active_function_writer = true;
  GW.num_functions++;
  ThisSymbolIdx = GW.symbol(F).symbol();
  auto sbomInfo = GW.getSbomInfoOfFunction(&F);
  if (sbomInfo.has_value()) {
    SbomComponentIdx = GW.string(sbomInfo->ComponentName);
    SbomVersionIdx = GW.string(sbomInfo->Version);
  } else {
    auto emp = GW.string("");
    SbomComponentIdx = emp;
    SbomVersionIdx = emp;
  }
}

FunctionWriter::~FunctionWriter() { assert(Written); }

Node FunctionWriter::freshLocal(StringRef Name) {
  auto Idx = num_locals++;
  if (record_debug_data) {
    GW.local_names.push_back(GW.string(Name));
  }
  return Node::local(ThisSymbolIdx, Idx);
}
void FunctionWriter::finish(uint32_t NumAllocas) {
  assert(!Written);
  Written = true;
  GW.has_active_function_writer = false;
  // Encode the actual edge data and then push it to function data.
  GW.function_data.push_back(as_u32(ThisSymbolIdx));
  GW.function_data.push_back(this->SbomComponentIdx);
  GW.function_data.push_back(this->SbomVersionIdx);
  GW.function_data.push_back(num_locals);
  GW.function_data.push_back(NumAllocas);
  // We unwrap this because it cannot fail.
  bracelet::unwrap(bracelet::edges::visitTuple(m_edges, [&](auto &storage) {
    constexpr size_t NUM_COMPONENTS =
        std::decay_t<decltype(storage)>::EdgeType::NUM_COMPONENTS;
    static_assert(NUM_COMPONENTS == 2 || NUM_COMPONENTS == 3);
    boost::sort::pdqsort_branchless(
        storage.data.begin(), storage.data.end(),
        [](const auto &x, const auto &y) {
          // TODO: Replace this component-wise comparison with a strict weak
          // ordering while preserving branchless evaluation if it is needed.
          // NOLINTBEGIN(clang-diagnostic-bitwise-instead-of-logical)
          if constexpr (NUM_COMPONENTS == 2) {
            return (x[0] < y[0]) & (x[1] < y[1]);
          } else if constexpr (NUM_COMPONENTS == 3) {
            return (x[0] < y[0]) & (x[1] < y[1]) & (x[2] < y[2]);
          } else {
            static_assert(false, "count must be 2 or 3 (or implement more)");
          }
          // NOLINTEND(clang-diagnostic-bitwise-instead-of-logical)
        });
    GW.function_data.push_back(as_u32(storage.data.size()));
    uint32_t previous_value = 0;
    size_t start_idx = GW.function_data.size();
    GW.function_data.resize(start_idx + storage.data.size() * NUM_COMPONENTS);
    for (const auto &edge : storage.data) {
      // We can use the uint32_t types here, since we assume a two's complement
      // encoding.
      uint32_t delta0 = edge[0] - previous_value;
      previous_value = edge[0];
      uint32_t delta1 = edge[1] - previous_value;
      previous_value = edge[1];
      GW.function_data[start_idx++] =
          bracelet::edges::encoding::zigzag_encode(delta0);
      GW.function_data[start_idx++] =
          bracelet::edges::encoding::zigzag_encode(delta1);
      if (NUM_COMPONENTS == 3)
        GW.function_data[start_idx++] = edge[2];
    }
    return bracelet::ok();
  }));
}

void GraphWriter::writeTo(Module &M) const {
  // Let's start by computing the blobs: DebugData and FunctionData.
  std::vector<uint8_t> function_data_uncompressed(
      streamvbyte_max_compressedbytes(as_u32(function_data.size())));
  auto function_data_uncompressed_len =
      streamvbyte_encode(function_data.data(), function_data.size(),
                         function_data_uncompressed.data());
  function_data_uncompressed.resize(function_data_uncompressed_len);
  std::vector<uint8_t> function_data_compressed(
      ZSTD_compressBound(function_data_uncompressed.size()));
  constexpr int COMPRESSION_LEVEL = 9;
  auto function_data_compressed_len = ZSTD_compress(
      function_data_compressed.data(), function_data_compressed.size(),
      function_data_uncompressed.data(), function_data_uncompressed.size(),
      COMPRESSION_LEVEL);
  assert(!ZSTD_isError(function_data_compressed_len));
  function_data_compressed.resize(function_data_compressed_len);

  // And now DebugData
  std::vector<uint8_t> debug_data_uncompressed(StringBlob.begin(),
                                               StringBlob.end());
  debug_data_uncompressed.resize(
      StringBlob.size() + streamvbyte_max_compressedbytes(local_names.size()));
  auto local_names_size = streamvbyte_delta_encode(
      local_names.data(), as_u32(local_names.size()),
      debug_data_uncompressed.data() + StringBlob.size(), 0);
  debug_data_uncompressed.resize(StringBlob.size() + local_names_size);
  std::vector<uint8_t> debug_data_compressed(
      ZSTD_compressBound(debug_data_uncompressed.size()));
  auto debug_data_compressed_len =
      ZSTD_compress(debug_data_compressed.data(), debug_data_compressed.size(),
                    debug_data_uncompressed.data(),
                    debug_data_uncompressed.size(), COMPRESSION_LEVEL);
  assert(!ZSTD_isError(debug_data_compressed_len));
  debug_data_compressed.resize(debug_data_compressed_len);
  DEBUG_WITH_TYPE(
      "braceletreachability",
      dbgs() << "Compressed debug data length: " << debug_data_compressed_len
             << " len as sz: " << debug_data_compressed.size()
             << " len as sz cast: " << as_u32(debug_data_compressed.size()));

  // Now we create the LLVM values to store everything.
  encoding::GraphHeader header = {
      .magic_number = encoding::GRAPH_MAGIC_NUMBER,
      .function_data_compressed_size = as_u32(function_data_compressed.size()),
      .debug_data_compressed_size = as_u32(debug_data_compressed.size()),
      .num_symbols = as_u32(Symbols.size()),
      .string_blob_length = as_u32(StringBlob.size()),
      .has_debug_locals = record_debug_data,
      .total_num_locals = as_u32(local_names.size()),
      .num_functions = num_functions,
      .function_array_length = as_u32(function_data.size()),
  };
  auto &ctx = M.getContext();
  auto *u8 = IntegerType::get(ctx, 8);
  auto *ptr_ty = PointerType::getUnqual(ctx);
  auto *debug_global_value =
      new GlobalVariable(M, ArrayType::get(u8, debug_data_compressed.size()),
                         true, GlobalValue::InternalLinkage,
                         ConstantDataArray::get(ctx, debug_data_compressed));
  debug_global_value->setAlignment(Align::Constant<1>());
  debug_global_value->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  debug_global_value->setComdat(OutputComdat);
  edges::encoding::SectionNames section_names(
      Triple(M.getTargetTriple()).getVendor() == Triple::VendorType::Apple);
  debug_global_value->setSection(section_names.debug);

  static_assert(sizeof(header) % 8 == 0, "No padding in header");
  auto *graph_data_ty = StructType::get(
      ArrayType::get(u8, sizeof(header)),     // header
      ptr_ty,                                 // pointer to debug data
      ArrayType::get(ptr_ty, Symbols.size()), // symbol array
      ArrayType::get(u8, function_data_compressed.size()) // function data
  );
  auto *graph_data_global = new GlobalVariable(
      M, graph_data_ty, true, GlobalValue::InternalLinkage,
      ConstantStruct::get(
          graph_data_ty,
          ConstantDataArray::get(
              ctx,
              ArrayRef(reinterpret_cast<uint8_t *>(&header), sizeof(header))),
          debug_global_value,
          ConstantArray::get(
              ArrayType::get(ptr_ty, Symbols.size()),
              ArrayRef(symbols_in_order.data(), symbols_in_order.size())),
          ConstantDataArray::get(ctx, function_data_compressed)));
  graph_data_global->setComdat(OutputComdat);
  graph_data_global->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  auto *SL = M.getDataLayout().getStructLayout(graph_data_ty);
  assert(SL->getAlignment().value() == 8);
  graph_data_global->setAlignment(SL->getAlignment());
  graph_data_global->setName(
      "graph"); // TODO: LLVM will dedeupe this name as needed, right?
  graph_data_global->setSection(section_names.graph);
  appendToUsed(M, {graph_data_global});
}
