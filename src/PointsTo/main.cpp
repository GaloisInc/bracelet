#include "CLI/CLI.hpp"
#include "ObjectParsing/ObjectParsing.h"
#include "PointsTo/PointsTo.h"
#include "Result/Result.h"
#include <absl/log/initialize.h>
#include <filesystem>
#include <iostream>
#include <optional>

using namespace bracelet;

namespace {
Result<int> mainResult(int argc, const char **argv) {
  absl::InitializeLog();
  CLI::App app;
  std::string Executable, Sysroot, CoreFile;
  std::filesystem::path tmp = "";
  std::filesystem::path trace_dir = "";
  std::filesystem::path svf_dir = "/opt/svf";
  std::optional<std::filesystem::path> clang_dir = std::nullopt;
  std::optional<std::filesystem::path> llvm_dir = std::nullopt;
  bool conservative_mode = false;
  bool save_pts = false;
  app.add_option("executable", Executable, "The executable to operate on")
      ->required();
  app.add_option("--svf-dir", svf_dir, "Install directory of SVF");
  app.add_option("--clang-dir", clang_dir, "Install directory of SVF-clang");
  app.add_option("--llvm-dir", llvm_dir, "Install directory of SVF-llvm");
  app.add_option("--sysroot", Sysroot, "Where to search for dynamic libraries");
  app.add_option("--core", CoreFile,
                 "Pull data from a coredump. (--sysroot is required)");
  app.add_option("--trace-dir", trace_dir,
                 "Check the static points-to results against these traces. "
                 "(Must be done with a coredump)");
  app.add_option("--tmp", tmp, "If provided, use this temporary directory");
  app.add_flag("--conservative", conservative_mode,
               "Apply conservative defaults for missing code");
  app.add_flag("--save-pts", save_pts, "Save points-to results to CSV");
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
    if (!trace_dir.empty()) {
      std::cerr << "Trace dir can only be given with a coredump\n";
      return 1;
    }
  }

  points_to::SVFInstall install_dirs = {svf_dir, clang_dir, llvm_dir};

  if (!trace_dir.empty()) {
    auto points_to = BRACELET_TRY(points_to::computePointsTo(
        *obj, conservative_mode, tmp.empty() ? save_pts : true, install_dirs,
        tmp.empty() ? nullptr : &tmp));
    BRACELET_TRY(points_to::checkPointsToAgainstTrace(
        *static_cast<object_parsing::CoredumpObject *>(obj.get()), points_to,
        trace_dir));
  } else {
    BRACELET_TRY(points_to::runPointsTo(*obj, conservative_mode, save_pts,
                                      install_dirs,
                                      tmp.empty() ? nullptr : &tmp));
  }
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
