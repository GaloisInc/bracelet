#include "Edges/Edges.h"
#include "Edges/Encoding.h"
#include "Edges/Reader.h"
#include "Result/Result.h"
#include "RuntimeFormat/RuntimeFormat.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/initialize.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include <CLI/CLI.hpp>
#include <absl/log/log.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

using namespace bracelet;

namespace {
auto format_hex(uint64_t num) { return absl::StrFormat("0x%016x", num); }

struct PrintingOptions {
  bool PrettyEdges;
  bool PrintCallArgumentCount;
};

template <typename T> struct OutputTable {
  using Edge = T;
  std::ofstream out;
};

Result<void> close_facts(std::ofstream &f) {
  CHECK(f.is_open());
  f.flush();
  BRACELET_ENSURE(f, "I/O error has occurred writing facts");
  f.close();
  return bracelet::ok();
}

std::filesystem::path PathOfSymbol(std::filesystem::path prefix,
                                   uint64_t symbol) {
  return prefix / absl::StrFormat("0x%016x", symbol);
}

Result<void> open_facts_for_symbol(std::filesystem::path prefix,
                                   std::ofstream &f, uint64_t symbol,
                                   std::string_view name,
                                   const char *suffix = ".facts") {
  CHECK(!f.is_open()) << name << "wasn't closed";
  auto symb_path = PathOfSymbol(prefix, symbol);
  auto path = symb_path / absl::StrFormat("%s%s", name, suffix);
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  BRACELET_TRY_CONTEXT(ec, "Can't create directory %v", path.parent_path());
  f.open(path);
  BRACELET_ENSURE(f.is_open(), "Cannot open %v: %s", path, strerror(errno));
  return ok();
}

struct EdgeOutputVisitor {
  EdgeOutputVisitor(
      std::filesystem::path prefix, object_parsing::Object &obj,
      std::optional<object_parsing::debug_info::SourceLocationMap> &slm,
      PrintingOptions printing_options)
      : prefix(prefix), obj(obj), slm(slm), printing_options(printing_options) {
  }

  Result<void> enter_function(
      edges::Node function, uint32_t num_allocas,
      edges::GraphDebugData debug_data,
      const std::optional<edges::encoding::SBOMInformation> &sbominfo) {
    this->function = function;
    this->num_allocas = num_allocas;
    this->debug_data = debug_data;
    function_name = BRACELET_TRY(obj.symbolName(function.symbol()));
    SeenNodes.clear();
    DefinedFunctions.clear();
    ForeignFunctions.clear();
    BRACELET_TRY(open_facts(DebugTable, "DebugTable"));
    BRACELET_TRY(open_facts(SbomTable, "SbomTable"));
    BRACELET_TRY(open_facts(IsSymbol, "IsSymbol"));
    BRACELET_TRY(open_facts(IsAlloca, "IsAlloca"));
    BRACELET_TRY(open_facts(ValueName, "ValueName"));
    BRACELET_TRY(open_facts(Starred, "Starred"));
    BRACELET_TRY(
        open_facts(EnclosingFunctionForLocal, "EnclosingFunctionForLocal"));
    BRACELET_TRY(open_facts(NodeInstructions, "NodeInstructions"));
    BRACELET_TRY(edges::visitTuple(OutputTables, [this](auto &output_table) {
      using Edge =
          typename std::remove_reference_t<decltype(output_table)>::Edge;
      return open_facts(output_table.out, (Edge::NAME == "DlsymPagePointer")
                                              ? "Dlsym"
                                              : Edge::NAME);
    }));
    std::ofstream name_txt;
    BRACELET_TRY(open_facts(name_txt, "function-name.txt", ""));
    name_txt << function_name;
    BRACELET_TRY(close_facts(name_txt));
    if (sbominfo) {
      BRACELET_TRY(printSbomInfo(*sbominfo, function));
    }

    return bracelet::ok();
  }
  Result<void> exit_function() {
    BRACELET_TRY(close_facts(DebugTable));
    BRACELET_TRY(close_facts(SbomTable));
    BRACELET_TRY(close_facts(IsSymbol));
    BRACELET_TRY(close_facts(IsAlloca));
    BRACELET_TRY(close_facts(ValueName));
    BRACELET_TRY(close_facts(Starred));
    BRACELET_TRY(close_facts(EnclosingFunctionForLocal));
    BRACELET_TRY(close_facts(NodeInstructions));
    BRACELET_TRY(edges::visitTuple(OutputTables, [](auto &output_table) {
      return close_facts(output_table.out);
    }));
    return bracelet::ok();
  }

  template <typename Edge> Result<void> visit_edge(const Edge &E) {
    BRACELET_TRY_CONTEXT(visit_edge_inner(E), "Processing function 0x%016x: %s",
                       function.symbol(), function_name);
    return ok();
  }

  template <typename Edge> Result<void> visit_edge_inner(const Edge &E) {
    // Ensure that the specializations are properly invoked.
    static_assert(!std::is_same_v<Edge, edges::DlsymPagePointer>, "");
    static_assert(!std::is_same_v<Edge, edges::Call>, "");
    if constexpr (Edge::NUM_COMPONENTS == 3)
      return printIndexedEdge<Edge>(std::get<0>(E), std::get<1>(E),
                                    std::get<2>(E));
    else
      return printSingletonEdge<Edge>(std::get<0>(E), std::get<1>(E));
  }
  Result<void> visit_edge_inner(const edges::DlsymPagePointer &E) {
    // TODO: use RuntimeFormat to actually parse this
    auto page_ptr_node = E.page_ptr();
    BRACELET_ENSURE(!page_ptr_node.local_idx(),
                  "page pointer should be a symbol");
    object_parsing::Address page;
    BRACELET_TRY_CONTEXT(
        obj.resolvePointers(boost::span(&page, 1), page_ptr_node.symbol()),
        "dlsym page initial pointer");
    while (page != 0) {
      uint64_t count;
      BRACELET_TRY_CONTEXT(
          obj.copyData(boost::span((uint8_t *)&count, sizeof(count)), page + 8),
          "dlsym page count");
      // 2046 comes from the dlsym RuntimeFormat struct. See the above note
      // about parsing this better.
      std::array<object_parsing::Address, 2046> pointers;
      BRACELET_TRY_CONTEXT(obj.resolvePointers(boost::span(pointers), page + 16),
                         "dlsym page body pointers");
      for (uint64_t i = 0; i < std::min(count, pointers.size()); i++) {
        BRACELET_TRY_CONTEXT(
            visit_edge_inner(edges::Assign(E.dlsym_output(),
                                           edges::Node::symbol(pointers[i]))),
            "Dlsym artifical assign edge");
      }
      BRACELET_TRY_CONTEXT(obj.resolvePointers(boost::span(&page, 1), page),
                         "dlsym page next pointer");
    }
    return printSingletonEdge<edges::DlsymPagePointer>(std::get<0>(E),
                                                       std::get<1>(E));
  }
  Result<void> visit_edge_inner(const edges::Call &E) {
    if (printing_options.PrintCallArgumentCount)
      return printIndexedEdge<bracelet::edges::Call>(
          std::get<0>(E), std::get<1>(E), std::get<2>(E));
    return printSingletonEdge<bracelet::edges::Call>(std::get<0>(E),
                                                   std::get<1>(E));
  }

  Result<void> printNode(std::ofstream &Dst, edges::Node N) {
    if (printing_options.PrettyEdges) {
      return printDebugNode(Dst, N);
    }
    printNodePure(Dst, N);
    return ok();
  }

private:
  Result<void> open_facts(std::ofstream &f, std::string_view name,
                          const char *suffix = ".facts") {
    return open_facts_for_symbol(prefix, f, function.symbol(), name, suffix);
    CHECK(!f.is_open()) << name << "wasn't closed";
    auto path = prefix / absl::StrFormat("0x%016x/%s%s", function.symbol(),
                                         name, suffix);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    BRACELET_TRY_CONTEXT(ec, "Can't create directory %v", path.parent_path());
    f.open(path);
    BRACELET_ENSURE(f.is_open(), "Cannot open %v: %s", path, strerror(errno));
    return ok();
  }

  Result<void>
  printSbomInfo(const bracelet::edges::encoding::SBOMInformation &Sbominfo,
                edges::Node forNode) {
    BRACELET_TRY(printNode(SbomTable, forNode));
    SbomTable << "\t" << Sbominfo.ComponentName;
    SbomTable << "\t" << Sbominfo.Version << "\n";
    return ok();
  }

  Result<void> emitNodeInfo(edges::Node N) {
    if (SeenNodes.count(N))
      return ok();
    SeenNodes.insert(N);
    BRACELET_TRY(emitNodeInfo(edges::Node::symbol(N.symbol())));
    if (auto local_idx = N.local_idx()) {
      auto IsAllocaFlag = local_idx < num_allocas;
      if (IsAllocaFlag) {
        BRACELET_TRY(printNode(IsAlloca, N));
        IsAlloca << "\n";
      }
      BRACELET_TRY(printNode(EnclosingFunctionForLocal, function));
      EnclosingFunctionForLocal << "\t";
      BRACELET_TRY(printNode(EnclosingFunctionForLocal, N));
      EnclosingFunctionForLocal << "\n";
    } else {
      BRACELET_TRY(printNode(IsSymbol, N));
      IsSymbol << "\n";
    }

    // TODO: The debug info for this mod wont contain a dlsym loaded symbol
    // So we dont print debug info here and need to check
    auto sym_name = obj.symbolName(N.symbol());
    if (sym_name.has_value()) {
      BRACELET_TRY(printNode(DebugTable, N));
      DebugTable << "\t";
      BRACELET_TRY(printDebugNode(DebugTable, N));
      DebugTable << "\n";
      if (printing_options.PrettyEdges) {
        printNodePure(DebugTable, N);
        DebugTable << "\t";
        BRACELET_TRY(printDebugNode(DebugTable, N));
        DebugTable << "\n";
      }
    } else {
      LOG(WARNING) << "Node associated with symbol: " << format_hex(N.symbol())
                   << " does not have a debug sym\n";
    }

    for (int I = 0; I < StarCount; I++) {
      BRACELET_TRY(printStarredNode(Starred, I + 1, N));
      Starred << "\t";
      BRACELET_TRY(printStarredNode(Starred, I, N));
      Starred << "\n";

      BRACELET_TRY(printNode(ValueName, N));
      ValueName << "\t";
      BRACELET_TRY(printStarredNode(ValueName, I, N));
      ValueName << "\n";
    }
    BRACELET_TRY(printFinalStarredNode(Starred, N));
    Starred << "\t";
    BRACELET_TRY(printStarredNode(Starred, StarCount, N));
    Starred << "\n";
    BRACELET_TRY(printFinalStarredNode(Starred, N));
    Starred << "\t";
    BRACELET_TRY(printFinalStarredNode(Starred, N));
    Starred << "\n";
    BRACELET_TRY(printNode(ValueName, N));
    ValueName << "\t";
    BRACELET_TRY(printFinalStarredNode(ValueName, N));
    ValueName << "\n";

    if (slm && N.local_idx()) {
      BRACELET_TRY(emitAddresses(N));
    }
    return ok();
  }

private:
  Result<void> emitAddresses(edges::Node N) {
    // TODO: we should put this information somewhere other than the debug
    // name
    CHECK(N.local_idx());
    if (!debug_data->has_locals() || !slm)
      return ok();
    std::string_view Name = BRACELET_TRY(debug_data->local_name(*N.local_idx()));
    auto Pipe = Name.find("|");
    if (Pipe == std::string::npos) {
      return ok();
    }

    // Trim the inlined from location if it was printed.
    auto InlineLoc = Name.find("@[");
    if (InlineLoc != std::string::npos) {
      Name = Name.substr(0, InlineLoc);
    }

    auto LineNoStart = Name.find(":");
    if (LineNoStart == std::string::npos) {
      return ok();
    }
    auto Space = Name.rfind(' ', LineNoStart);
    auto Slash = Name.rfind('/', LineNoStart);
    if (Space == std::string::npos) {
      Space = 0;
    }
    if (Slash == std::string::npos) {
      Slash = 0;
    }
    auto FileNameStart = std::max(Space, Slash);
    assert(FileNameStart != std::string::npos);
    assert(LineNoStart > FileNameStart);
    auto FileName = Name.substr(
        FileNameStart + ((Space == 0 && Slash == 0) ? 0 : 1),
        LineNoStart - FileNameStart - ((Space == 0 && Slash == 0) ? 0 : 1));
    auto ColumnNoStart = Name.find(":", LineNoStart + 1);
    if (ColumnNoStart == std::string::npos) {
      return ok();
    }
    auto LineNoStr =
        Name.substr(LineNoStart + 1, ColumnNoStart - LineNoStart - 1);
    if (LineNoStr.find("0") == 0) {
      return ok();
    }
    auto EndOfColumnNo =
        std::min(Name.find("|", ColumnNoStart), Name.find(" ", ColumnNoStart));
    if (EndOfColumnNo == std::string::npos) {
      return ok();
    }
    auto ColumnNoStr =
        Name.substr(ColumnNoStart + 1, EndOfColumnNo - ColumnNoStart - 1);
    uint32_t LineNo, ColumnNo;
    BRACELET_ENSURE(absl::SimpleAtoi(LineNoStr, &LineNo),
                  "Invalid line number: %s", LineNoStr);
    BRACELET_ENSURE(absl::SimpleAtoi(ColumnNoStr, &ColumnNo),
                  "Invalid column number: %s", ColumnNoStr);
    auto SourcePos =
        object_parsing::debug_info::SourceLocation{FileName, LineNo, ColumnNo};
    auto It = slm->find(SourcePos);
    if (It != slm->end()) {
      for (const auto &Range : It->second) {
        for (uint64_t Addr = Range.start; Addr <= Range.end; Addr++) {
          NodeInstructions << format_hex(Addr) << "\t";
          BRACELET_TRY(printNode(NodeInstructions, N));
          NodeInstructions << "\n";
        }
      }
    }
    return ok();
  }
  Result<void> printDebugNode(std::ofstream &Out, edges::Node N) {
    auto symbol_name = BRACELET_TRY(obj.symbolName(N.symbol()));
    if (symbol_name.empty())
      Out << "<UnknownSymbol>";
    else
      Out << symbol_name;
    if (auto local_idx = N.local_idx()) {
      Out << " -> ";
      if (debug_data->has_locals()) {
        CHECK_EQ(N.symbol(), function.symbol());
        Out << BRACELET_TRY(debug_data->local_name(*local_idx));
      } else {
        Out << "<DebugDataMissing>";
      }
    }
    return ok();
  }
  static void printNodePure(std::ofstream &Dst, edges::Node N) {
    auto Local = N.local_idx();
    Dst << format_hex(N.symbol());
    if (Local) {
      Dst << ":" << absl::StreamFormat("0x%04x", *Local);
    }
  }

  static constexpr int StarCount = 3;
  static void printStars(std::ofstream &Dst, int N) {
    for (int I = 0; I < N; I++) {
      Dst << "*";
    }
  }
  Result<void> printStarredNode(std::ofstream &Dst, int Stars, edges::Node N) {
    printStars(Dst, Stars);
    return printNode(Dst, N);
  }
  Result<void> printFinalStarredNode(std::ofstream &Dst, edges::Node N) {
    Dst << "$";
    return printStarredNode(Dst, StarCount, N);
  }

  template <typename T>
  Result<void> printSingletonEdge(edges::Node To, edges::Node From) {
    auto &Out = std::get<OutputTable<T>>(OutputTables).out;
    BRACELET_TRY(printNode(Out, To));
    Out << "\t";
    BRACELET_TRY(printNode(Out, From));
    Out << "\n";
    BRACELET_TRY(emitNodeInfo(To));
    BRACELET_TRY(emitNodeInfo(From));
    return ok();
  }
  template <typename T>
  Result<void> printIndexedEdge(edges::Node To, edges::Node From,
                                uint32_t Idx) {
    auto &Out = std::get<OutputTable<T>>(OutputTables).out;
    BRACELET_TRY(printNode(Out, To));
    Out << "\t";
    BRACELET_TRY(printNode(Out, From));
    Out << "\t";
    Out << Idx;
    Out << "\n";
    BRACELET_TRY(emitNodeInfo(To));
    BRACELET_TRY(emitNodeInfo(From));
    return ok();
  }

  std::filesystem::path prefix;
  object_parsing::Object &obj;
  std::optional<object_parsing::debug_info::SourceLocationMap> &slm;
  PrintingOptions printing_options;

  absl::flat_hash_set<edges::Node> SeenNodes;
  absl::flat_hash_set<edges::Node> DefinedFunctions;
  absl::flat_hash_set<edges::Node> ForeignFunctions;
  std::ofstream DebugTable;
  std::ofstream SbomTable;
  std::ofstream IsSymbol;
  std::ofstream IsAlloca;
  std::ofstream ValueName;
  std::ofstream Starred;
  std::ofstream EnclosingFunctionForLocal;
  std::ofstream NodeInstructions;
  edges::EdgeTuple<OutputTable> OutputTables;

  edges::Node function;
  std::string function_name;
  uint32_t num_allocas;
  std::optional<edges::GraphDebugData> debug_data;
};

Result<int> mainResult(int argc, const char **argv) {
  absl::InitializeLog();
  CLI::App app;
  std::string Executable, Sysroot, CoreFile;
  PrintingOptions PO = {false, false};
  app.add_option("executable", Executable, "The executable to operate on")
      ->required();
  app.add_option("--sysroot", Sysroot, "Where to search for dynamic libraries");
  app.add_option("--core", CoreFile,
                 "Pull data from a coredump. (--sysroot is required)");
  app.add_flag("--pretty-edges", PO.PrettyEdges,
               "Use pretty names for all edges. This helps with debugging, but "
               "might be less correct than using addresses for everything.");
  app.add_flag("--call-arg-count", PO.PrintCallArgumentCount,
               "Emit the number of arguments for each call.");
  CLI11_PARSE(app, argc, argv);
  std::unique_ptr<object_parsing::Object> obj;
  if (!CoreFile.empty()) {
    if (Sysroot.empty()) {
      std::cerr
          << "If a coredump is specified, a sysroot must be specified too.\n";
      return 1;
    }
    obj = BRACELET_TRY_CONTEXT(
        object_parsing::openCore(Executable, Sysroot, CoreFile),
        "Opening coredump %s for executable %s with sysroot %s", CoreFile,
        Executable, Sysroot);
  } else {
    obj = BRACELET_TRY_CONTEXT(object_parsing::openObject(Executable),
                             "Opening object %s", Executable);
  }
  auto slm = BRACELET_TRY(obj->sourceLocationMap());
  std::filesystem::path out = ".";
  EdgeOutputVisitor eov(out, *obj, slm, PO);
  BRACELET_TRY(
      edges::readEdges(*obj, [&](edges::FunctionInfo &fi) -> Result<void> {
        BRACELET_TRY(eov.enter_function(fi.function, fi.num_allocas,
                                      fi.debug_data, fi.sbom_info));
        BRACELET_TRY(
            fi.visitAllEdges([&](auto &edge) { return eov.visit_edge(edge); }));
        BRACELET_TRY(eov.exit_function());
        return ok();
      }));
  edges::encoding::SectionNames section_names(obj->isApple());
  auto trace_sites = out / "trace_sites";
  std::error_code ec;
  std::filesystem::create_directories(trace_sites, ec);
  BRACELET_TRY_CONTEXT(ec, "mkdir -p %v", trace_sites);
  std::vector<uint8_t> trace_site;
  BRACELET_TRY_CONTEXT(
      obj->visitSections(
          [&](const object_parsing::Section &section) -> Result<void> {
            if (section.name == section_names.trace_site) {
              trace_site.resize(section.size);
              std::ofstream out(trace_sites /
                                absl::StrFormat("0x%016x.bin", section.start));
              BRACELET_ENSURE(out.is_open(),
                            "couldn't open destination for trace site");
              BRACELET_TRY(obj->copyData(boost::span(trace_site), section.start));
              out.write((char *)trace_site.data(), trace_site.size());
              out.flush();
              BRACELET_ENSURE(out.good(), "failed to write trace sites");
              out.close();
            }
            return ok();
          }),
      "Dumping trace sites");
  if (obj->SupportsFastAddrNodeMap()) {
    BRACELET_TRY(obj->visitCUAddressMaps(
        [out, &eov](const bracelet::object_parsing::CoredumpObject::CUCallsites
                        &callsites) -> Result<void> {
          for (auto it = callsites.begin(); it != callsites.end();
               it = callsites.upper_bound(it->first)) {
            // We check that the symbol already exists, this filters for null
            // low_pc i dont think this can happen because of our static address
            // encoding but just to be safe
            if (std::filesystem::exists(PathOfSymbol(out, it->first))) {
              std::ofstream stream;
              BRACELET_TRY(open_facts_for_symbol(out, stream, it->first,
                                               "FastNodeInstructions"));
              auto rng = callsites.equal_range(it->first);
              for (auto elem = rng.first; elem != rng.second; elem++) {
                stream << format_hex(elem->second.callsiteAddr) << "\t";
                BRACELET_TRY(eov.printNode(stream, elem->second.nodeID));
                stream << "\n";
              }
            }
          }
          return bracelet::ok();
        }));
  }
  BRACELET_TRY(obj->visitCUInlineEdges(
        [out, &eov](const bracelet::object_parsing::CoredumpObject::CUInlineEdges
                        &inline_edges) -> Result<void> {
          for (auto it = inline_edges.begin(); it != inline_edges.end();
               it = inline_edges.upper_bound(it->first)) {
            // We check that the symbol already exists, this filters for null
            // low_pc i dont think this can happen because of our static address
            // encoding but just to be safe
            if (std::filesystem::exists(PathOfSymbol(out, it->first))) {
              std::ofstream stream;
              BRACELET_TRY(open_facts_for_symbol(out, stream, it->first,
                                               "InlineFunctions"));
              auto rng = inline_edges.equal_range(it->first);
              for (auto elem = rng.first; elem != rng.second; elem++) {
		BRACELET_TRY(eov.printNode(stream, elem->second.inlinerID));
		stream << "\t" << elem->second.inlinedName << "\n";
              }
            }
          }
          return bracelet::ok();
	}));
  return 0;
}

} // namespace

int main(int argc, const char **argv) {
  auto r = mainResult(argc, argv);
  if (r.has_value()) {
    return r.value();
  } else {
    r.error().print(std::cerr);
    return 1;
  }
}
