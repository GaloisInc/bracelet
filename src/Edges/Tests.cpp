// Emit object files, executables, and coredumps for random graphs, and parse
// them to make sure the results we read are what we expected.

#include "ObjectParsing/ObjectParsing.h"
#include "Reader.h"
#include "Subprocess/Subprocess.h"
#include "Writer.h"
#include "absl/strings/numbers.h"
#include "boost/core/span.hpp"
#include "catch2/generators/catch_generators_all.hpp"
#include "tempfile/tempfile.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/FileSystem.h"
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/strings/str_format.h>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <memory>
#include <mutex>
#include <random>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
std::once_flag initialize_llvm_flag;
void initialize_llvm() {
  std::call_once(initialize_llvm_flag, []() {
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();
  });
}

using CanonicalFunctionId = size_t;
using CanonicalNode = bracelet::edges::Node;
using WriterNode = bracelet::edges::Node;
using ReaderNode = bracelet::edges::Node;

constexpr std::string_view FUNCTION_NAME_DELIMETER = "___";
std::string functionNameCreate(std::string_view module_name,
                               CanonicalFunctionId id) {
  return absl::StrFormat("%s%s%v", module_name, FUNCTION_NAME_DELIMETER, id);
}
std::tuple<std::string_view, CanonicalFunctionId>
functionNameExtract(std::string_view function_name) {
  auto pos = function_name.find(FUNCTION_NAME_DELIMETER);
  REQUIRE(pos != std::string_view::npos);
  auto module_name = function_name.substr(0, pos);
  auto id_str = function_name.substr(pos + FUNCTION_NAME_DELIMETER.size());
  CanonicalFunctionId id = 0;
  REQUIRE(absl::SimpleAtoi(id_str, &id));
  return std::make_tuple(module_name, id);
}

struct CanonicalFunction {
  CanonicalFunction(const CanonicalFunction &) = delete;
  CanonicalFunction &operator=(const CanonicalFunction &) = delete;

  CanonicalFunctionId id;
  llvm::Function *function;
  bool extern_function = false;
  bracelet::edges::EdgeTuple<absl::flat_hash_set> canonical_edges;
  std::vector<std::string> local_names;
  unsigned num_alloca;

  operator CanonicalNode() const { return CanonicalNode::symbol(id); }

  void add_edge(bracelet::edges::AnyEdge e) {
    REQUIRE_FALSE(extern_function);
    std::visit(
        [&](auto e) {
          std::array<CanonicalNode, 2> nodes = {std::get<0>(e), std::get<1>(e)};
          for (auto node : nodes) {
            if (auto local = node.local_idx()) {
              REQUIRE(node.symbol() == id);
              REQUIRE(local < local_names.size());
            }
          }
          std::get<absl::flat_hash_set<decltype(e)>>(canonical_edges).insert(e);
        },
        e);
  }
  CanonicalNode fresh_local(std::string name) {
    REQUIRE_FALSE(extern_function);
    auto out = bracelet::edges::Node::local(id, local_names.size());
    local_names.emplace_back(std::move(name));
    return out;
  }
};

struct CompilationUnit {
  CompilationUnit(std::string name,
                  std::vector<std::unique_ptr<CanonicalFunction>> &functions)
      : functions(functions), name(name) {
    initialize_llvm();
    ctx = std::make_unique<llvm::LLVMContext>();
    module = std::make_unique<llvm::Module>(name, *ctx);
    auto triple = llvm::sys::getDefaultTargetTriple();
    module->setTargetTriple(triple);
    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(triple, error);
    if (!target) {
      INFO(error);
    }
    REQUIRE(target);
    llvm::TargetOptions opt;
    target_machine = target->createTargetMachine(triple, "generic", "", opt,
                                                 llvm::Reloc::PIC_);
    module->setDataLayout(target_machine->createDataLayout());
  }
  auto &new_function(unsigned num_alloca) {
    auto id = functions.size();
    auto *f = llvm::Function::Create(
        llvm::FunctionType::get(llvm::Type::getInt32Ty(*ctx), {}, false),
        llvm::Function::ExternalLinkage, functionNameCreate(name, id),
        module.get());
    auto bb = llvm::BasicBlock::Create(*ctx, "entry", f);
    llvm::IRBuilder<> builder(*ctx);
    builder.SetInsertPoint(bb);
    builder.CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(*ctx), id));
    llvm::verifyFunction(*f);
    std::unique_ptr<CanonicalFunction> &out =
        functions.emplace_back(new CanonicalFunction{
            .id = id,
            .function = f,
            .extern_function = false,
            .num_alloca = num_alloca,
        });
    defined_function_count++;
    return *out;
  }
  auto &new_extern_function() {
    auto id = functions.size();
    auto *f = llvm::Function::Create(
        llvm::FunctionType::get(llvm::Type::getVoidTy(*ctx), false),
        llvm::Function::ExternalLinkage, functionNameCreate(name, id),
        module.get());
    std::unique_ptr<CanonicalFunction> &out =
        functions.emplace_back(new CanonicalFunction{
            .id = id,
            .function = f,
            .extern_function = true,
            .num_alloca = 0,
        });
    return *out;
  }
  void emit(const std::filesystem::path &obj_file) {
    REQUIRE_FALSE(emitted);
    emitted = true;
    bracelet::edges::GraphWriter gw(nullptr, true);
    for (auto &f : functions) {
      if (f->extern_function)
        continue;
      // This means that the test is invalid
      REQUIRE(f->num_alloca <= f->local_names.size());
      bracelet::edges::FunctionWriter fw(gw, *f->function, true);
      std::vector<WriterNode> fw_locals;
      for (const auto &name : f->local_names) {
        fw_locals.push_back(fw.freshLocal(name));
      }
      bracelet::unwrap(
          bracelet::edges::visitTuple(f->canonical_edges, [&](auto edge_set) {
            for (auto edge : edge_set) {
              using Edge = decltype(edge);
              constexpr size_t NUM_COMPONENTS = Edge::NUM_COMPONENTS;
              auto process = [&](CanonicalNode canonical_node) -> WriterNode {
                if (auto local = canonical_node.local_idx()) {
                  return fw_locals[*local];
                } else {
                  return gw.symbol(
                      *functions.at(canonical_node.symbol())->function);
                }
              };
              if constexpr (NUM_COMPONENTS == 2) {
                fw.addEdge(Edge(process(std::get<0>(edge)),
                                process(std::get<1>(edge))));
              } else {

                fw.addEdge(Edge(process(std::get<0>(edge)),
                                process(std::get<1>(edge)), std::get<2>(edge)));
              }
            }
            return bracelet::ok();
          }));
      fw.finish(f->num_alloca);
    }
    gw.writeTo(*module);

    std::error_code error;
    llvm::raw_fd_ostream dest(obj_file.c_str(), error, llvm::sys::fs::OF_None);
    if (error) {
      INFO(error.message());
      REQUIRE(false);
    }
    llvm::legacy::PassManager pass;
    REQUIRE_FALSE(target_machine->addPassesToEmitFile(
        pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile));
    pass.run(*module);
    dest.flush();
  }

  std::unique_ptr<llvm::LLVMContext> ctx;
  std::unique_ptr<llvm::Module> module;
  llvm::TargetMachine *target_machine;
  std::vector<std::unique_ptr<CanonicalFunction>> &functions;
  bool emitted = false;
  size_t defined_function_count = 0;
  std::string name;
};

struct FunctionCheckState {
  FunctionCheckState() {}
  bool seen = false;
  bracelet::edges::EdgeTuple<absl::flat_hash_set> saw_edges;
};

void check_compilation_unit(
    CompilationUnit &cu, bracelet::object_parsing::Object &target,
    std::vector<std::unique_ptr<CanonicalFunction>> &functions,
    bool parse_debug) {
  absl::flat_hash_map<uint64_t, CanonicalFunctionId> symbol2functionid_cache;
  std::vector<FunctionCheckState> check_state(functions.size());
  auto symbol2functionid = [&](uint64_t sym) {
    auto iter = symbol2functionid_cache.find(sym);
    if (iter != symbol2functionid_cache.end())
      return iter->second;
    std::string_view function_name = bracelet::unwrap(target.symbolName(sym));
    auto [_, function_id] = functionNameExtract(function_name);
    REQUIRE(function_id < functions.size());
    symbol2functionid_cache[sym] = function_id;
    return function_id;
  };
  auto canonicalize_node = [&](ReaderNode node) {
    auto sym = symbol2functionid(node.symbol());
    if (auto local_idx = node.local_idx())
      return CanonicalNode::local(sym, *local_idx);
    else
      return CanonicalNode::symbol(sym);
  };
  bracelet::unwrap(bracelet::edges::readEdges(
      target, [&](bracelet::edges::FunctionInfo &fi) -> bracelet::Result<void> {
        REQUIRE(!fi.function.local_idx());
        std::string_view function_name =
            bracelet::unwrap(target.symbolName(fi.function.symbol()));
        auto [cu_name, function_id] = functionNameExtract(function_name);
        REQUIRE(cu_name == cu.name);
        REQUIRE(function_id < functions.size());
        auto current_function = functions[function_id].get();
        REQUIRE(!check_state.at(current_function->id).seen);
        REQUIRE(!current_function->extern_function);
        CHECK(current_function->num_alloca == fi.num_allocas);
        CHECK(current_function->local_names.size() == fi.num_locals);
        check_state.at(current_function->id).seen = true;
        if (parse_debug) {
          REQUIRE(fi.debug_data.has_locals());
          REQUIRE(current_function->local_names.size() ==
                  fi.debug_data.num_locals());
          for (size_t i = 0; i < fi.debug_data.num_locals(); i++) {
            CHECK(current_function->local_names[i] ==
                  bracelet::unwrap(fi.debug_data.local_name(i)));
          }
        } else {
          // TODO(Ian): we should set this by selecting a write flag that
          // decides if we are going to emit debug data
          // CHECK(!fi.debug_data.has_locals());
        }
        bracelet::unwrap(
            fi.visitAllEdges([&](auto &edge) -> bracelet::Result<void> {
              using Edge = typename std::decay_t<decltype(edge)>;
              auto &saw_edges = std::get<absl::flat_hash_set<Edge>>(
                  check_state.at(current_function->id).saw_edges);
              auto to = canonicalize_node(std::get<0>(edge));
              auto from = canonicalize_node(std::get<1>(edge));
              if constexpr (Edge::NUM_COMPONENTS == 2)
                saw_edges.insert(Edge(to, from));
              else
                saw_edges.insert(Edge(to, from, std::get<2>(edge)));
              return bracelet::ok();
            }));
        return bracelet::ok();
      }));
  for (auto &f : functions) {
    if (!f->extern_function) {
      CHECK(check_state.at(f->id).seen);
      bracelet::unwrap(bracelet::edges::visitTuple(
          check_state.at(f->id).saw_edges, [&](auto &saw) {
            using Edge =
                typename std::remove_reference_t<decltype(saw)>::key_type;
            auto &canonical =
                std::get<absl::flat_hash_set<Edge>>(f->canonical_edges);
            CHECK(saw.size() == canonical.size());
            for (const auto &e : saw) {
              if (!canonical.contains(e))
                absl::PrintF("edge %v shouldn't be there\n", e);
            }
            for (const auto &e : canonical) {
              if (!saw.contains(e))
                absl::PrintF("missing edge %v\n", e);
            }
            CHECK((saw == canonical));
            return bracelet::ok();
          }));
    }
  }
}

void check_compilation_unit(
    CompilationUnit &cu, bracelet::object_parsing::Object &target,
    std::vector<std::unique_ptr<CanonicalFunction>> &functions) {
  check_compilation_unit(cu, target, functions, true);
  check_compilation_unit(cu, target, functions, false);
}

void run_test(std::function<void(CompilationUnit &)> cb) {
  bracelet::tempfile::TemporaryDirectory tmp =
      bracelet::unwrap(bracelet::tempfile::TemporaryDirectory::create());
  std::filesystem::path obj =
      tmp.path() /
      absl::StrFormat("%s.o", Catch::getResultCapture().getCurrentTestName());
  std::vector<std::unique_ptr<CanonicalFunction>> functions;
  CompilationUnit cu("cu", functions);
  cb(cu);
  cu.emit(obj);
  SECTION("object") {
    auto target = bracelet::unwrap(bracelet::object_parsing::openObject(obj));
    check_compilation_unit(cu, *target, functions);
  }
  auto c_path = tmp.path() / "test.c";
  std::ofstream c_src(c_path);
  REQUIRE(c_src.is_open());
  c_src << "#include <stdio.h>\n#include <unistd.h>\n#include "
           "<stdlib.h>\n#include <assert.h>\n";
  for (const auto &f : functions) {
    if (f->extern_function) {
      absl::Format(&c_src, "int %s(void) {return %d;}\n",
                   functionNameCreate(cu.name, f->id), f->id);
    } else {
      absl::Format(&c_src, "int %s(void);\n",
                   functionNameCreate(cu.name, f->id));
    }
  }
  c_src << "int main() {\n";
  for (const auto &f : functions) {
    if (!f->extern_function) {
      absl::Format(&c_src, "assert(%s() == %d);\n",
                   functionNameCreate(cu.name, f->id), f->id);
    }
  }
  c_src << "char buf[1024];\nsnprintf(buf, sizeof(buf), \"gcore %d\", "
           "(int)getpid());\nreturn system(buf);\n}\n";
  c_src.flush();
  REQUIRE(c_src.good());
  c_src.close();
  std::ofstream coredump_filter("/proc/self/coredump_filter");
  REQUIRE(coredump_filter.is_open());
  coredump_filter << "0xffff";
  coredump_filter.close();
  auto a_out = tmp.path() / "a.out";
  std::vector<std::string_view> args = {
      "-o", a_out.c_str(), "-Wl,--emit-relocs", c_path.c_str(), obj.c_str()};
  REQUIRE(bracelet::unwrap(bracelet::subprocess::call("cc", boost::span(args))) ==
          0);
  SECTION("executable") {

    auto linked_target =
        bracelet::unwrap(bracelet::object_parsing::openObject(a_out));
    check_compilation_unit(cu, *linked_target, functions);
  }
  auto core_dir = tmp.path() / "core";
  std::filesystem::create_directory(core_dir);
  REQUIRE(bracelet::unwrap(bracelet::subprocess::call(
              a_out.c_str(), boost::span<std::string_view>(), &core_dir)) == 0);
  std::vector<std::filesystem::path> elems;
  for (const auto &dentry : std::filesystem::directory_iterator(core_dir))
    elems.emplace_back(dentry.path());
  REQUIRE(elems.size() == 1);
  std::filesystem::path corefile = elems[0];
  SECTION("coredump") {
    auto core_target =
        bracelet::unwrap(bracelet::object_parsing::openCore(a_out, "/", corefile));
    check_compilation_unit(cu, *core_target, functions);
  }
}

template <typename Rng, typename T>
T &sample_choice(Rng &rng, boost::span<T> list) {
  REQUIRE_FALSE(list.empty());
  std::uniform_int_distribution<size_t> dist(0, list.size() - 1);
  auto idx = dist(rng);
  REQUIRE(idx < list.size());
  return list[idx];
}
} // namespace

TEST_CASE("empty") {
  run_test([](CompilationUnit &) {});
}
TEST_CASE("simple") {
  run_test([](CompilationUnit &cu) {
    auto &f = cu.new_function(1);
    auto l0 = f.fresh_local("l0");
    auto l1 = f.fresh_local("l1");
    auto l2 = f.fresh_local("l2");
    f.add_edge(bracelet::edges::Return(f, l0));
    f.add_edge(bracelet::edges::Assign(l1, l2));
    f.add_edge(bracelet::edges::Assign(l0, l2));
    f.add_edge(bracelet::edges::ArgumentDefinition(l0, f, 23));
  });
}

TEST_CASE("simple2") {
  run_test([](CompilationUnit &cu) {
    auto &f = cu.new_function(1);
    auto l0 = f.fresh_local("l0");
    auto l1 = f.fresh_local("l1");
    auto l2 = f.fresh_local("l2");
    f.add_edge(bracelet::edges::Return(f, l0));
    f.add_edge(bracelet::edges::Assign(l1, l2));
    f.add_edge(bracelet::edges::Assign(l0, l2));
    f.add_edge(bracelet::edges::ArgumentDefinition(l0, f, 23));
    auto &f2 = cu.new_function(1);
    l0 = f2.fresh_local("l0");
    l1 = f2.fresh_local("l1");
    l2 = f2.fresh_local("l2");
    f2.add_edge(bracelet::edges::Return(f2, l0));
    f2.add_edge(bracelet::edges::Assign(l1, f));
    f2.add_edge(bracelet::edges::Assign(l0, l2));
    f2.add_edge(bracelet::edges::ArgumentDefinition(l0, f2, 6));
  });
}

TEST_CASE("simple3") {
  run_test([](CompilationUnit &cu) {
    auto &ef = cu.new_extern_function();
    auto &f = cu.new_function(1);
    auto l0 = f.fresh_local("l0");
    auto l1 = f.fresh_local("l1");
    auto l2 = f.fresh_local("l2");
    f.add_edge(bracelet::edges::Return(f, l0));
    f.add_edge(bracelet::edges::Assign(ef, l2));
    f.add_edge(bracelet::edges::Assign(l0, l1));
    f.add_edge(bracelet::edges::ArgumentDefinition(l0, f, 23));
  });
}

TEST_CASE("random") {
  uint64_t seed = GENERATE(take(16, random(0ULL, 1ULL << 63)));
  std::mt19937_64 rng(seed);
  run_test([&](CompilationUnit &cu) {
    std::vector<CanonicalFunction *> defined_functions;
    defined_functions.reserve(64);
    for (size_t i = 0; i < 64; i++)
      defined_functions.push_back(
          &cu.new_function(std::uniform_int_distribution(0, 6)(rng)));
    for (size_t i = 0; i < 16; i++)
      cu.new_extern_function();
    for (auto *f : defined_functions) {
      std::vector<CanonicalNode> locals;
      auto num_locals = std::uniform_int_distribution(f->num_alloca, 32U)(rng);
      locals.reserve(num_locals);
      for (size_t i = 0; i < num_locals; i++) {
        locals.push_back(f->fresh_local(
            absl::StrFormat("The local for function %d #%d", f->id, i)));
      }
      // We just visiting this tuple to enumerate thru edge types.
      bracelet::edges::EdgeTuple<std::optional> hack_tuple;
      bracelet::unwrap(bracelet::edges::visitTuple(hack_tuple, [&](auto &saw) {
        using Edge =
            typename std::remove_reference_t<decltype(saw)>::value_type;
        auto num_edges = std::uniform_int_distribution(0, 32)(rng);
        for (int i = 0; i < num_edges; i++) {
          CanonicalNode to =
              std::bernoulli_distribution(0.25)(rng) || locals.empty()
                  ? *sample_choice(rng, boost::span(cu.functions))
                  : sample_choice(rng, boost::span(locals));
          CanonicalNode from =
              std::bernoulli_distribution(0.25)(rng) || locals.empty()
                  ? *sample_choice(rng, boost::span(cu.functions))
                  : sample_choice(rng, boost::span(locals));
          if constexpr (Edge::NUM_COMPONENTS == 2)
            f->add_edge(Edge(to, from));
          else {
            auto idx = std::uniform_int_distribution(0, 512)(rng);
            f->add_edge(Edge(to, from, idx));
          }
        }
        return bracelet::ok();
      }));
    }
  });
}
