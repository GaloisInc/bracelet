#include "PointsTo.h"
#include "Edges/Edges.h"
#include "Edges/Encoding.h"
#include "Edges/Reader.h"
#include "BraceletRuntimeStructs_lldb.h"
#include "ObjectParsing/LldbStringifyUtils.h"
#include "ObjectParsing/ObjectParsing.h"
#include "Subprocess/Subprocess.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "lldb/API/SBTarget.h"
#include "tempfile/tempfile.h"
#include <boost/operators.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <regex>
#include <stdio.h>
#include <thread>
#include <type_traits>
#include <variant>

using namespace bracelet;

namespace {
void write_makefile(std::ostream &out_stream) {
#include "template_svf.Makefile.inc"
}

// This node is undefined
struct NodeStateUndefined : boost::equality_comparable<NodeStateUndefined> {
  bool operator==(const NodeStateUndefined &) const { return true; }
};
// This node is global data/variable
struct NodeStateGlobalData : boost::equality_comparable<NodeStateGlobalData> {
  bool operator==(const NodeStateGlobalData &) const { return true; }
};
// This node is a function with the given number of arguments.
struct NodeStateFunction : boost::equality_comparable<NodeStateFunction> {
  explicit NodeStateFunction(uint16_t nargs) : nargs(nargs) {}
  uint16_t nargs;
  bool operator==(const NodeStateFunction &other) const {
    return other.nargs == nargs;
  }
};
// NodeState represents what we know about a node. It forms a lattice with
// "undefined" at the bottom, and the other node states above that.
using NodeState =
    std::variant<NodeStateUndefined, NodeStateGlobalData, NodeStateFunction>;

// A utility to pretty-print a C identifier for a symbol along with its name
struct SymbolRendering {
  SymbolRendering(object_parsing::Object &obj, object_parsing::Address address)
      : obj(obj), address(address) {}

  template <typename Sink>
  friend void AbslStringify(Sink &sink, const SymbolRendering &sn) {
    std::string_view name = "<unknown>";
    auto name_result = sn.obj.symbolName(sn.address);
    if (name_result.has_value()) {
      name = name_result.value();
    } else {
      LOG(WARNING) << "Could not get name of symbol "
                   << absl::StreamFormat("0x%016x", sn.address) << ": "
                   << name_result.error();
    }
    absl::Format(&sink, "NODE_0x%016x /* %s */", sn.address, name);
  }

private:
  object_parsing::Object &obj;
  object_parsing::Address address;
};

template <typename Sink> void AbslStringify(Sink &sink, const NodeState &ns) {
  std::visit(
      [&](const auto &ns) {
        using T = std::decay_t<decltype(ns)>;
        if constexpr (std::is_same_v<T, NodeStateUndefined>)
          absl::Format(&sink, "NodeStateUndefined");
        else if constexpr (std::is_same_v<T, NodeStateGlobalData>)
          absl::Format(&sink, "NodeStateGlobalData");
        else if constexpr (std::is_same_v<T, NodeStateFunction>)
          absl::Format(&sink, "NodeStateFunction(%d)", ns.nargs);
        else
          static_assert(false, "");
      },
      ns);
}

// A utility to pretty-print a C identifier for a Node
struct NodeRendering {
  NodeRendering(edges::Node node, edges::FunctionInfo &fi,
                object_parsing::Object &obj)
      : node(node), fi(fi), obj(obj) {}
  template <typename Sink>
  friend void AbslStringify(Sink &sink, const NodeRendering &nr) {
    if (nr.node.local_idx()) {
      assert(nr.node.symbol() == nr.fi.function.symbol());
      absl::Format(&sink, "local_%d_%v", *nr.node.local_idx(),
                   SymbolRendering(nr.obj, nr.node.symbol()));
    } else {
      absl::Format(&sink, "%v", SymbolRendering(nr.obj, nr.node.symbol()));
    }
  }

  friend std::ostream &operator<<(std::ostream &os, const NodeRendering &nr) {
    os << absl::StreamFormat("%v", nr);
    return os;
  }

private:
  edges::Node node;
  edges::FunctionInfo &fi;
  object_parsing::Object &obj;
};

struct EmitC {
  EmitC(object_parsing::Object &obj) : obj(obj) {}
  void populate_prelude(std::ostream &out_stream) {
#include "template_prelude.txt.inc"
  }
  // Populate node states and override usage
  Result<void> initial_pass() {
    return edges::readEdges(obj, [&](edges::FunctionInfo &fi) -> Result<void> {
      // According to ArgumentDefinition nodes, how many arguments does this
      // function have?
      unsigned current_function_nargs = 0;
      auto result = fi.visitAllEdges([&](auto edge) -> Result<void> {
        // visit edge
        using Edge = std::decay_t<decltype(edge)>;
        if constexpr (std::is_same_v<Edge, edges::Call>) {
          edges::Node callee = edge.callee();
          if (!callee.local_idx()) {
            BRACELET_TRY(setNargs(callee, NodeStateFunction(edge.nargs())));
          }
        } else if constexpr (std::is_same_v<Edge, edges::Store>) {
          // If we store into a global, assume that it's not a function.
          if (!edge.addr().local_idx()) {
            BRACELET_TRY(setNargs(edge.addr(), NodeStateGlobalData{}));
          }
        } else if constexpr (std::is_same_v<Edge, edges::ArgumentDefinition>) {
          current_function_nargs =
              std::max(current_function_nargs, edge.arg_no() + 1);
        }
        std::array<edges::Node, 2> out_edges = {std::get<0>(edge),
                                                std::get<1>(edge)};
        for (edges::Node out : out_edges) {
          if (out.symbol() != fi.function.symbol()) {
            assert(!out.local_idx());
            BRACELET_TRY(setNargs(out, NodeStateUndefined{}));
            if (!node_checked_for_override.contains(out)) {
              node_checked_for_override.insert(out);
              auto full_name = BRACELET_TRY(obj.symbolName(out.symbol()));
              auto name = full_name.substr(0, full_name.find('@'));
              auto iter = override_nargs.find(name);
              if (iter != override_nargs.end()) {
                // Use iter->first because that string_view is 'static
                override_nodes[out] = iter->first;
                BRACELET_TRY(setNargs(out, NodeStateFunction(iter->second)));
              }
            }
          }
        }
        return ok();
      });
      // We don't use BRACELET_TRY above because it messes up setting
      // breakpoints in the above code.
      BRACELET_TRY(std::move(result));
      BRACELET_TRY(
          setNargs(fi.function, NodeStateFunction(current_function_nargs)));
      return ok();
    });
  }

  auto renderSymbol(object_parsing::Address addr) {
    return SymbolRendering(obj, addr);
  }

  // set the state for node to node_state, erroring out if the two node_states
  // are incompatible (i.e. they're non-equal non-undefined states)
  Result<void> setNargs(edges::Node node, NodeState node_state) {
    assert(!node.local_idx());
    auto iter = node_states.find(node);
    if (iter == node_states.end())
      node_states[node] = node_state;
    else if (std::holds_alternative<NodeStateUndefined>(iter->second))
      iter->second = node_state;
    else if (std::holds_alternative<NodeStateUndefined>(node_state)) {
      // Do nothing.
    } else
      BRACELET_ENSURE(iter->second == node_state,
                    "nargs mismatch for node %v. Current: %v; New: %v",
                    renderSymbol(node.symbol()), iter->second, node_state);
    return ok();
  }

  Result<void> writeFunctions(const std::filesystem::path &dst,
                              bool conservative_mode) {
    {
      std::ofstream out_stream(dst / "globals.c");
      BRACELET_ENSURE(out_stream.is_open(), "Cannot open %v: %s",
                    dst / "globals.c", strerror(errno));
#include "template_define_globals.txt.inc"
      out_stream.flush();
      BRACELET_ENSURE(out_stream.good(), "Failed to write c file");
    }
    auto result =
        edges::readEdges(obj, [&](edges::FunctionInfo &fi) -> Result<void> {
          auto out_c = dst / absl::StrFormat("0x%016x.c", fi.function.symbol());
          auto this_function_node_state = node_states.at(fi.function);
          assert(std::holds_alternative<NodeStateFunction>(
              this_function_node_state));
          declared_functions.erase(fi.function);
          auto this_function_nargs =
              std::get<NodeStateFunction>(this_function_node_state).nargs;
          std::ofstream out_stream(out_c);
          BRACELET_ENSURE(out_stream.is_open(), "Cannot open %v: %s", out_c,
                        strerror(errno));
          auto show_node = [&](edges::Node node) {
            return NodeRendering(node, fi, obj);
          };
#include "template_c.txt.inc"
          out_stream.flush();
          BRACELET_ENSURE(out_stream.good(), "Failed to write c file");
          return ok();
        });
    auto missing = dst / "missing.txt";
    std::ofstream missing_stream(missing);
    BRACELET_ENSURE(missing_stream.is_open(), "Cannot open %v: %s", missing,
                  strerror(errno));
    for (const auto &sym : declared_functions) {
      std::string_view name = "<unknown>";
      auto name_result = obj.symbolName(sym.symbol());
      if (name_result.has_value()) {
        name = name_result.value();
      }
      missing_stream << absl::StrFormat("0x%016x\t%s\n", sym.symbol(), name);
    }
    missing_stream.flush();
    BRACELET_ENSURE(missing_stream.good(), "Failed to write missing report");
    if (conservative_mode) {
      for (const auto &sym : declared_functions) {
        auto out_c = dst / absl::StrFormat("0x%016x.c", sym.symbol());
        auto this_function_node_state = node_states.at(sym);
        assert(std::holds_alternative<NodeStateFunction>(
            this_function_node_state));
        auto this_function_nargs =
            std::get<NodeStateFunction>(this_function_node_state).nargs;
        std::ofstream out_stream(out_c);
        BRACELET_ENSURE(out_stream.is_open(), "Cannot open %v: %s", out_c,
                      strerror(errno));
        // clang-format off
#include "template_prelude.txt.inc"
#include "template_conservative.txt.inc"
        // clang-format on
        out_stream.flush();
      }
    }
    BRACELET_TRY(std::move(result));
    return ok();
  }

  void writePrototype(std::ostream &out, object_parsing::Address symbol,
                      unsigned nargs) {
    out << absl::StreamFormat("T %v(", renderSymbol(symbol));
    if (nargs == 0)
      out << "void";
    else {
      out << "T arg0";
      for (unsigned i = 1; i < nargs; i++) {
        out << absl::StreamFormat(", T arg%d", i);
      }
    }
    out << ")";
  }

  Result<void> emitC(const std::filesystem::path &dst, bool conservative_mode) {
    std::ofstream prelude_h(dst / "prelude.h");
    BRACELET_ENSURE(prelude_h.is_open(), "Cannot open %v: %s", dst / "prelude.h",
                  strerror(errno));
    populate_prelude(prelude_h);
    prelude_h << "\n\n";
    BRACELET_TRY_CONTEXT(initial_pass(), "initial edge pass");
    for (const auto &pair : node_states) {
      if (std::holds_alternative<NodeStateFunction>(pair.second)) {
        declared_functions.insert(pair.first);
      }
    }
    for (const auto &pair : override_nodes) {
      declared_functions.erase(pair.first);
    }
    for (auto &[node, state] : node_states) {
      if (std::holds_alternative<NodeStateUndefined>(state)) {
        LOG(WARNING) << "Node " << BRACELET_TRY(obj.symbolName(node.symbol()))
                     << " " << node << " is undefined.";
      }
      if (std::holds_alternative<NodeStateFunction>(state)) {
        writePrototype(prelude_h, node.symbol(),
                       std::get<NodeStateFunction>(state).nargs);
        prelude_h << ";\n";
      } else {
        prelude_h << absl::StreamFormat("extern T %v;\n",
                                        renderSymbol(node.symbol()));
      }
    }
    for (auto [node, name] : override_nodes) {
      override_nodes_by_name[name] = node;
      prelude_h << absl::StreamFormat("#define %v OVERRIDE(%s)\n",
                                      renderSymbol(node.symbol()), name);
    }
    prelude_h.flush();
    BRACELET_ENSURE(prelude_h.good(), "Cannot write prelude.h");
    prelude_h.close();

    std::ofstream makefile(dst / "Makefile");
    BRACELET_ENSURE(makefile.is_open(), "Cannot open %v: %s", dst / "Makefile",
                  strerror(errno));
    write_makefile(makefile);
    makefile.flush();
    BRACELET_ENSURE(makefile.good(), "Cannot write Makefile");
    makefile.close();

    std::ofstream globals(dst / "globals.c");
    BRACELET_ENSURE(globals.is_open(), "Cannot open %v: %s", dst / "globals.c",
                  strerror(errno));
    globals << "#include \"prelude.h\"\n";
    for (auto &[node, state] : node_states) {
      if (!std::holds_alternative<NodeStateFunction>(state)) {
        auto name = renderSymbol(node.symbol());
        globals << absl::StreamFormat(
            "T %v;\nvoid bracelet_global_init_%v(void) { %v = "
            "malloc(__nondeterministic_choice()); }\n",
            name, name, name);
      }
    }
    globals.flush();
    BRACELET_ENSURE(globals.good(), "Cannot write globals");
    globals.close();
    BRACELET_TRY(writeFunctions(dst, conservative_mode));
    return bracelet::ok();
  }

  object_parsing::Object &obj;
  absl::flat_hash_map<edges::Node, NodeState> node_states;
  absl::flat_hash_set<edges::Node> declared_functions;
  // TODO: we could probably improve perf by adding this as a field to
  // NodeState.
  // This stores for each node whether we've looked up its symbol name to see if
  // it should be replaced by an override.
  absl::flat_hash_set<edges::Node> node_checked_for_override;
  // 'static strings
  absl::flat_hash_map<std::string_view, uint16_t> override_nargs;
  // 'static strings
  // If a node has an override map it to the name of its override.
  absl::flat_hash_map<edges::Node, std::string_view> override_nodes;
  // 'static strings
  absl::flat_hash_map<std::string_view, edges::Node> override_nodes_by_name;
};

struct Svf {
  static Result<Svf> create(const points_to::SVFInstall &install_paths) {
    Svf svf(install_paths);
    BRACELET_TRY(svf.init());
    return svf;
  }

  Result<void> make(const std::filesystem::path &work_dir) {
    std::string env0 =
        absl::StrFormat("SVF_CLANG=%v", SVF_CLANG_PATH / "bin/clang");
    std::string env1 =
        absl::StrFormat("SVF_CLANGXX=%v", SVF_CLANG_PATH / "bin/clang++");
    std::string env2 =
        absl::StrFormat("SVF_LLVM_LINK=%v", SVF_LLVM_PATH / "bin/llvm-link");
    std::string jobs =
        absl::StrFormat("-j%d", std::thread::hardware_concurrency());
    auto rc = BRACELET_TRY(
        call(work_dir, "env", {env0, env1, env2, "make", "linked.bc", jobs}));
    BRACELET_ENSURE(rc == 0, "make failed");
    return ok();
  }

  Result<void> points_to(const std::filesystem::path &work_dir, bool save_pts) {
    std::string cmd = absl::StrFormat(
        "pushd %s; source setup.sh >&2; popd; set -euxo pipefail; bracelet "
        "-ander -ind-call-limit=4294967295 "
        "-extapi=%s "
        "%s -bracelet-callgraph=cg.csv linked.bc",
        SVF_PATH, SVF_PATH / "lib" / "extapi.bc",
        save_pts ? "-bracelet-pointsto=pts.csv" : "");
    auto rc = BRACELET_TRY(call(work_dir, "bash", {"-c", cmd}));
    BRACELET_ENSURE(rc == 0, "svf failed");
    return ok();
  }

private:
  Result<int> call(const std::filesystem::path &work_dir, std::string_view cmd,
                   std::initializer_list<std::string_view> args) {
    if (!docker) {
      return subprocess::call(cmd, args, &work_dir);
    }
    CHECK(work_dir.is_absolute()) << work_dir << "isn't an absolute path!";
    std::string mount = absl::StrFormat("%v:%v", work_dir, work_dir);
    std::vector<std::string_view> full_args = {
        "run", "--rm", "--workdir", work_dir.native(),
        "-v",  mount,  SVF_IMAGE,   cmd};
    for (auto arg : args)
      full_args.push_back(arg);
    return subprocess::call(*docker, boost::span(full_args));
  }

  Svf(const points_to::SVFInstall &install_paths)
      : SVF_PATH(install_paths.SVF_DIR) {
    if (install_paths.SVF_CLANG) {
      this->SVF_CLANG_PATH = *install_paths.SVF_CLANG;
    } else {
      this->SVF_CLANG_PATH = this->SVF_PATH / "llvm-16.0.0.obj";
    }

    if (install_paths.SVF_LLVM) {
      this->SVF_LLVM_PATH = *install_paths.SVF_LLVM;
    } else {
      this->SVF_LLVM_PATH = this->SVF_PATH / "llvm-16.0.0.obj";
    }
  }
  Result<void> init() {
    std::error_code ec;
    auto is_dir = std::filesystem::is_directory(SVF_PATH, ec);
    if (!is_dir || ec) {
      // We're not running in the SVF container. We'll need docker or podman.
      std::array alts = {"podman", "docker"};
      for (std::string_view alt : alts) {
        auto result = subprocess::call(alt, {"--version"});
        if (result.has_value() && result.value() == 0) {
          docker = alt;
          break;
        }
      }
      BRACELET_ENSURE(docker,
                    "SVF isn't installed, so you need docker or podman");
      // Pull the docker image if needed.
      auto rc = BRACELET_TRY(
          subprocess::call(*docker, {"run", "--rm", SVF_IMAGE, "true"}));
      BRACELET_ENSURE(rc == 0, "Pulling SVF docker image failed");
    }
    return ok();
  }

  std::optional<std::string> docker;
  std::filesystem::path SVF_PATH;
  std::filesystem::path SVF_CLANG_PATH;
  std::filesystem::path SVF_LLVM_PATH;
  static constexpr std::string_view SVF_IMAGE =
      "gitlab.ebossproject.com:5005/galois/svf/svf:galois-3.1";
};
} // namespace

Result<void> points_to::runPointsTo(object_parsing::Object &obj,
                                    bool conservative_mode, bool save_pts,
                                    const points_to::SVFInstall &install_paths,
                                    const std::filesystem::path *tmp_input) {
  std::optional<tempfile::TemporaryDirectory> tmp_dir;
  std::filesystem::path tmp;
  if (tmp_input) {
    tmp = *tmp_input;
  } else {
    tmp_dir = BRACELET_TRY(tempfile::TemporaryDirectory::create());
    tmp = tmp_dir->path();
  }
  EmitC emit_c(obj);
  BRACELET_TRY_CONTEXT(emit_c.emitC(tmp, conservative_mode), "emitting C code");
  std::cerr << "done emitting C code\n";
  Svf svf = BRACELET_TRY(Svf::create(install_paths));
  BRACELET_TRY_CONTEXT(svf.make(tmp), "building c code");
  std::cerr << "done compiling C code\n";
  BRACELET_TRY_CONTEXT(svf.points_to(tmp, save_pts), "running points to");
  std::cerr << "done running SVF\n";
  return ok();
}

Result<points_to::PointsToEdges>
points_to::computePointsTo(object_parsing::Object &obj, bool conservative_mode,
                           bool save_pts, const SVFInstall &install_paths,
                           const std::filesystem::path *tmp_input) {
  std::optional<tempfile::TemporaryDirectory> tmp_dir;
  std::filesystem::path tmp;
  if (tmp_input) {
    tmp = *tmp_input;
  } else {
    tmp_dir = BRACELET_TRY(tempfile::TemporaryDirectory::create());
    tmp = tmp_dir->path();
  }
  EmitC emit_c(obj);
  BRACELET_TRY_CONTEXT(emit_c.emitC(tmp, conservative_mode), "emitting C code");
  std::cerr << "done emitting C code\n";
  Svf svf = BRACELET_TRY(Svf::create(install_paths));
  BRACELET_TRY_CONTEXT(svf.make(tmp), "building c code");
  std::cerr << "done compiling C code\n";
  BRACELET_TRY_CONTEXT(svf.points_to(tmp, save_pts), "running points to");
  std::cerr << "done running SVF\n";
  std::ifstream pts_csv(tmp / "pts.csv");
  BRACELET_ENSURE(pts_csv.is_open(), "Cannot open points-to results");
  PointsToEdges out;
  const std::regex pts_entry(
      "(0x[0-9a-fA-F]+)\t([0-9]+|-)\t(0x[0-9a-fA-F]+)\t([0-9]+|-)",
      std::regex_constants::extended);
  for (std::string line; std::getline(pts_csv, line);) {
    std::smatch match;
    if (std::regex_match(line, match, pts_entry)) {
      auto src_addr = std::stoull(match[1], nullptr, 16);
      auto src = match[2] == "-"
                     ? edges::Node::symbol(src_addr)
                     : edges::Node::local(src_addr, std::stoull(match[2]));
      auto dst_addr = std::stoull(match[3], nullptr, 16);
      auto dst = match[4] == "-"
                     ? edges::Node::symbol(dst_addr)
                     : edges::Node::local(dst_addr, std::stoull(match[4]));
      out.insert(std::pair(src, dst));
    } else {
      BRACELET_ENSURE(false, "failed to parse points-to results: %s", line);
    }
  }
  std::cerr << "done parsing points-to results\n";
  return out;
}

Result<void>
points_to::checkPointsToAgainstTrace(object_parsing::CoredumpObject &obj,
                                     PointsToEdges &static_edges,
                                     const std::filesystem::path &traces_dir) {
  edges::encoding::SectionNames section_names(obj.isApple());
  absl::flat_hash_map<object_parsing::Address, edges::Node> trace_sites;
  BRACELET_TRY_CONTEXT(
      obj.visitSections([&](const object_parsing::Section &s) -> Result<void> {
        if (s.name != section_names.trace_site)
          return ok();
        auto end = s.start + s.size;
        for (object_parsing::Address addr = s.start;
             addr + sizeof(runtime_format_lldb::BraceletTraceSite) <= end;
             addr += sizeof(runtime_format_lldb::BraceletTraceSite)) {
          auto ts = BRACELET_TRY_CONTEXT(
              runtime_format_lldb::BraceletTraceSite::load(obj, addr),
              "Loading BraceletTraceSite at 0x%016x", addr);
          trace_sites[addr] = edges::Node::local(ts.function.ptr, ts.local_idx);
        }
        return ok();
      }),
      "Parsing tracesites");
  std::error_code ec;
  std::filesystem::directory_iterator iter(traces_dir, ec);
  BRACELET_TRY_CONTEXT(ec, "Reading directory %v", traces_dir);
  lldb::SBTarget target = obj.getTarget();
  std::vector<runtime_format_lldb::BraceletTraceEdge> buffer(size_t{1024} * 1024);
  absl::flat_hash_set<std::pair<edges::Node, edges::Node>> missing_edges;
  size_t trace_edge_count = 0;
  for (const auto &dentry : iter) {
    FILE *f = fopen(dentry.path().c_str(), "rb");
    BRACELET_ENSURE(f != nullptr, "Cannot open %v: %s", dentry.path(),
                  strerror(errno));
    // TODO: Replace this raw FILE lifetime with an RAII wrapper.
    // NOLINTBEGIN(clang-analyzer-unix.Stream)
    while (true) {
      auto rc =
          fread(buffer.data(), sizeof(runtime_format_lldb::BraceletTraceEdge),
                buffer.size(), f);
      if (rc == 0) {
        BRACELET_ENSURE(feof(f), "Failed to read from %v: %s", dentry.path(),
                      strerror(errno));
        break;
      }
      for (size_t i = 0; i < rc; i++) {
        auto trace_site_addr = (buffer[i].trace_site >> 1) << 1;
        if (trace_site_addr == 0)
          continue;
        auto ts_cursor = trace_sites.find(trace_site_addr);
        BRACELET_ENSURE(ts_cursor != trace_sites.end(),
                      "Cannot find trace site 0x%016x", trace_site_addr);
        auto trace_site_node = ts_cursor->second;
        edges::Node value_node;
        bool check = false;
        auto value_cursor = trace_sites.find(buffer[i].value);
        if (value_cursor != trace_sites.end()) {
          value_node = value_cursor->second;
          check = true;
        } else {
          auto addr = target.ResolveLoadAddress(buffer[i].value);
          if (addr.IsValid()) {
            BRACELET_ENSURE(addr.IsValid(), "Cannot resolve address 0x%016x",
                          buffer[i].value);
            auto symbol = addr.GetSymbol();
            if (symbol.IsValid()) {
              BRACELET_ENSURE(symbol.IsValid(),
                            "Cannot get symbol from address (%v) 0x%016x", addr,
                            buffer[i].value);
              auto symbol_start = symbol.GetStartAddress();
              BRACELET_ENSURE(symbol_start.IsValid(),
                            "Cannot get start address from address 0x%016x",
                            buffer[i].value);
              value_node =
                  edges::Node::symbol(symbol_start.GetLoadAddress(target));
              check = true;
            } else {
              absl::flat_hash_map<edges::Node, std::string> debug_names;
              BRACELET_TRY(edges::readEdges(
                  obj, [&](edges::FunctionInfo &fi) -> Result<void> {
                    if (!fi.debug_data.has_locals())
                      return ok();
                    for (unsigned i = 0; i < fi.num_locals; i++) {
                      debug_names[edges::Node::local(fi.function.symbol(), i)] =
                          BRACELET_TRY(fi.debug_data.local_name(i));
                    }
                    return ok();
                  }));
              auto print = [&](edges::Node n) -> Result<void> {
                std::cerr << BRACELET_TRY(obj.symbolName(n.symbol()));
                if (n.local_idx()) {
                  std::cerr << ":";
                  auto iter = debug_names.find(n);
                  if (iter == debug_names.end())
                    std::cerr << "<" << *n.local_idx() << ">";
                  else
                    std::cerr << iter->second;
                }
                return ok();
              };
              BRACELET_TRY(print(trace_site_node));
              std::cerr << "WARNING: get symbol from address "
                        << buffer[i].value << "\n";
            }
          } else {
            std::cerr << "WARNING: cannot resolve address " << buffer[i].value
                      << "\n";
          }
        }
        if (check) {
          // Now we check whether (trace_site_node, value_node) is represented
          // in the static graph.
          auto edge = std::make_pair(trace_site_node, value_node);
          trace_edge_count++;
          if (!static_edges.contains(edge)) {
            missing_edges.insert(edge);
          }
        }
      }
    }
    fclose(f);
    // NOLINTEND(clang-analyzer-unix.Stream)
  }
  if (!missing_edges.empty()) {
    absl::flat_hash_map<edges::Node, std::string> debug_names;
    BRACELET_TRY(
        edges::readEdges(obj, [&](edges::FunctionInfo &fi) -> Result<void> {
          if (!fi.debug_data.has_locals())
            return ok();
          for (unsigned i = 0; i < fi.num_locals; i++) {
            debug_names[edges::Node::local(fi.function.symbol(), i)] =
                BRACELET_TRY(fi.debug_data.local_name(i));
          }
          return ok();
        }));
    auto print = [&](edges::Node n) -> Result<void> {
      std::cerr << BRACELET_TRY(obj.symbolName(n.symbol()));
      if (n.local_idx()) {
        std::cerr << ":";
        auto iter = debug_names.find(n);
        if (iter == debug_names.end())
          std::cerr << "<" << *n.local_idx() << ">";
        else
          std::cerr << iter->second;
      }
      return ok();
    };
    for (auto [dst, src] : missing_edges) {
      std::cerr << "Missing edge: ";
      BRACELET_TRY(print(dst));
      std::cerr << " (" << dst.raw();
      if (auto idx = dst.local_idx()) {
        std::cerr << ", " << dst.symbol() << " " << *idx;
        std::cerr << ")";
      } else {
        std::cerr << ", " << dst.symbol() << ")";
      }
      std::cerr << " -> ";
      BRACELET_TRY(print(src));
      std::cerr << " (" << src.raw();
      if (auto idx = src.local_idx()) {
        std::cerr << ", " << src.symbol() << " " << *idx;
        std::cerr << ")";
      } else {
        std::cerr << ", " << src.symbol() << ")";
      }
      std::cerr << "\n";
    }
  }
  BRACELET_ENSURE(missing_edges.empty(), "Missing %d/%d edges",
                missing_edges.size(), trace_edge_count);
  std::cerr << "All " << trace_edge_count << " edges were found\n";
  return ok();
}
