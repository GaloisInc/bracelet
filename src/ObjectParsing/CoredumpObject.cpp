// Load an executable, a core dump, and loaded libraries from a snapshot. Reads
// BRACELET metadata and returns a `CoredumpObject`, a subclass of `Object`.
// Entrypoint: `object_parsing::openCore`.

#include "Edges/Encoding.h"
#include "LldbStringifyUtils.h"
#include "ObjectParsing.h"
#include "Result/Result.h"
#include "Result/ResultLLDB.h"
#include "absl/container/flat_hash_map.h"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBCommandReturnObject.h"
#include "lldb/API/SBFileSpec.h"
#include "lldb/API/SBTarget.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Object/ObjectFile.h"
#include <algorithm>
#include <absl/log/check.h>
#include <absl/log/log.h>
#include <cstdint>
#include <iomanip>
#include <lldb/API/SBAddress.h>
#include <lldb/API/SBDebugger.h>
#include <lldb/API/SBModule.h>
#include <lldb/API/SBSection.h>
#include <lldb/API/SBStream.h>
#include <lldb/API/SBSymbol.h>
#include <llvm/DebugInfo/DWARF/DWARFContext.h>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

using namespace bracelet;
using namespace object_parsing;

namespace {
std::once_flag lldb_initialized;

template <typename Cb>
Result<void> walkSections(lldb::SBSection &section, Cb cb) {
  for (uint32_t i = 0; i < section.GetNumSubSections(); i++) {
    lldb::SBSection subsection = section.GetSubSectionAtIndex(i);
    BRACELET_TRY(cb(subsection));
    BRACELET_TRY(walkSections(subsection, cb));
  }
  return bracelet::ok();
}
template <typename Cb>
Result<void> walkSections(lldb::SBModule &module, Cb cb) {
  for (uint32_t i = 0; i < module.GetNumSections(); i++) {
    lldb::SBSection section = module.GetSectionAtIndex(i);
    BRACELET_TRY(cb(section));
    BRACELET_TRY(walkSections(section, cb));
  }
  return bracelet::ok();
}
template <typename Cb>
Result<void> walkSections(lldb::SBTarget &target, Cb cb) {
  for (uint32_t i = 0; i < target.GetNumModules(); i++) {
    lldb::SBModule module = target.GetModuleAtIndex(i);
    BRACELET_TRY(walkSections(module, cb));
  }
  return bracelet::ok();
}

class LLDBLoadInfo : public llvm::LoadedObjectInfo {
public:
  // TODO we can get section content... seems a bit expensive tho not sure what
  // the best approach is

  Result<uint64_t> translateAddress(Address secaddr) const {
    auto file_off = mod.ResolveFileAddress(secaddr);
    BRACELET_ENSURE(file_off.IsValid(),
                  "Translated dwarf address %x should be valid", secaddr);
    return file_off.GetLoadAddress(target);
  }

  virtual uint64_t
  getSectionLoadAddress(const llvm::object::SectionRef &Sec) const {
    auto lldbSec = mod.GetSectionAtIndex(Sec.getIndex());

    return lldbSec.GetLoadAddress(target);
  }

  virtual std::unique_ptr<LoadedObjectInfo> clone() const {
    return std::make_unique<LLDBLoadInfo>(this->target, this->mod);
  }

  LLDBLoadInfo(lldb::SBTarget tgt, lldb::SBModule module)
      : target(tgt), mod(module) {}

private:
  // TODO yikes
  mutable lldb::SBTarget target;
  mutable lldb::SBModule mod;
};

struct CoredumpObjectImpl : public CoredumpObject {
  explicit CoredumpObjectImpl(lldb::SBTarget target) : target(target) {}
  virtual ~CoredumpObjectImpl() {}
  virtual bool isApple() override {
    std::string_view triple(target.GetTriple());
    return triple.find("apple") != std::string_view::npos ||
           triple.find("darwin") != std::string_view::npos;
  }
  virtual lldb::SBTarget &getTarget() override { return target; }
  // The Data& shouldn't be used after the callback returns.
  virtual Result<void> visitSections(
      std::function<Result<void>(const Section &)> callback) override {
    return walkSections(target, [&](lldb::SBSection &s) {
      std::string_view name = s.GetName();
      // Strip off the PT_LOAD[##] prefix
      auto pos = name.find(']');
      if (pos != std::string_view::npos)
        name = name.substr(pos + 1);
      return callback(Section{
          .name = name,
          .start = s.GetLoadAddress(target),
          .size = s.GetByteSize(),
      });
    });
  }

  virtual bool SupportsFastAddrNodeMap() override { return true; }

  virtual Result<void> visitDwarfContexts(
      std::function<Result<void>(std::unique_ptr<llvm::DWARFContext>,
                                 Object::AddressTranslation addrTranslation)>
          visit) override {
    for (size_t i = 0; i < this->target.GetNumModules(); i++) {
      lldb::SBModule mod = this->target.GetModuleAtIndex(i);
      if (mod.IsFileBacked()) {
        lldb::SBFileSpec fspec = mod.GetFileSpec();

        char dst_pth[260];
        fspec.GetPath(dst_pth, sizeof(dst_pth));
        auto [llObj, _] =
            BRACELET_TRY(openLLVMObject(std::filesystem::path(dst_pth)));
        auto loadInfo = LLDBLoadInfo(this->target, mod);
        auto dwf = llvm::DWARFContext::create(
            *llObj, llvm::DWARFContext::ProcessDebugRelocations::Process,
            &loadInfo);
        BRACELET_TRY(visit(std::move(dwf), [&loadInfo](auto addr) {
          return loadInfo.translateAddress(addr);
        }));
      }
    }

    return bracelet::ok();
  }

  virtual Result<const std::string_view> symbolName(Address addr) override {
    auto iter = symbol_names.find(addr);
    if (iter != symbol_names.end())
      return iter->second;
    auto sbaddr = target.ResolveLoadAddress(addr);
    BRACELET_ENSURE(sbaddr.IsValid(), "Invalid load address %x", addr);
    const char *name = nullptr;
    auto symbol = sbaddr.GetSymbol();
    if (symbol.IsValid()) {
      name = symbol.GetMangledName();
      if (name == nullptr)
        name = symbol.GetName();
    }
    if (name == nullptr) {
      LOG(WARNING) << "LLDB: Cannot find name for "
                   << absl::StreamFormat("0x%0x16", addr) << "/" << sbaddr
                   << "/" << symbol;
      name = "<unknown>";
    }
    return symbol_names.emplace(addr, name).first->second;
  }
  virtual Result<void> copyData(boost::span<uint8_t> dst,
                                Address addr) override {
    if (dst.empty())
      return bracelet::ok();
    auto sbaddr = target.ResolveLoadAddress(addr);
    BRACELET_ENSURE(sbaddr.IsValid(), "Invalid load address %x", addr);
    lldb::SBError error;
    auto read_bytes = target.ReadMemory(sbaddr, dst.data(), dst.size(), error);
    BRACELET_TRY_CONTEXT(error, "Reading %d bytes from address %v", dst.size(),
                       sbaddr);
    BRACELET_ENSURE(read_bytes == dst.size(),
                  "Only read %d bytes from address %v (%d bytes wanted)",
                  read_bytes, sbaddr, dst.size());
    return bracelet::ok();
  }
  virtual Result<void> resolvePointers(boost::span<Address> dst,
                                       Address addr) override {
    return copyData(
        boost::span(reinterpret_cast<uint8_t *>(dst.data()), dst.size_bytes()),
        addr);
  }
  virtual Result<std::optional<debug_info::SourceLocationMap>>
  sourceLocationMap() override {
    debug_info::SourceLocationMap slm;
    for (uint32_t I = 0; I < target.GetNumModules(); I++) {
      auto Module = target.GetModuleAtIndex(I);
      for (uint32_t J = 0; J < Module.GetNumCompileUnits(); J++) {
        auto CompileUnit = Module.GetCompileUnitAtIndex(J);
        for (uint32_t K = 0; K < CompileUnit.GetNumLineEntries(); K++) {
          auto LineEntry = CompileUnit.GetLineEntryAtIndex(K);
          slm[debug_info::SourceLocation{
                  .file = LineEntry.GetFileSpec().GetFilename(),
                  .line = LineEntry.GetLine(),
                  .col = LineEntry.GetColumn()}]
              .push_back({LineEntry.GetStartAddress().GetLoadAddress(target),
                          LineEntry.GetEndAddress().GetLoadAddress(target)});
        }
      }
    }
    return slm;
  }

private:
  lldb::SBTarget target;
  absl::flat_hash_map<Address, std::string> symbol_names;
};

} // namespace

Result<std::unique_ptr<CoredumpObject>>
object_parsing::openCore(const std::filesystem::path &exe,
                         const std::filesystem::path &sysroot,
                         const std::filesystem::path &core_file) {
  std::call_once(lldb_initialized, []() {
    auto error = lldb::SBDebugger::InitializeWithErrorHandling();
    CHECK(error.Success()) << "Failed to initalize LLDB: "
                           << error.GetCString();
  });
  auto dbg = lldb::SBDebugger::Create();
  lldb::SBError error;
  // Defer dependent-module loading until the snapshot sysroot is configured.
  // Otherwise LLDB may bind modules from matching paths in the host Nix store.
  auto target = dbg.CreateTarget(exe.c_str(), nullptr, "remote-linux",
                                 /*add_dependent_modules=*/false, error);
  BRACELET_TRY_CONTEXT(error, "LLDB loading target: %v", exe);
  std::error_code error_sys;
  auto sysroot_abs = std::filesystem::absolute(sysroot, error_sys);
  BRACELET_TRY_CONTEXT(error_sys, "Taking absolute path of %v", sysroot);
  target.GetPlatform().SetSDKRoot(sysroot_abs.c_str());

  // Core files record absolute module paths. Remap those paths into the
  // snapshot before LLDB attempts to resolve any modules from the host.
  std::ostringstream object_map_command;
  object_map_command << "settings set target.object-map " << std::quoted("/")
                     << " " << std::quoted(sysroot_abs.string());
  lldb::SBCommandReturnObject command_result;
  dbg.GetCommandInterpreter().HandleCommand(object_map_command.str().c_str(),
                                            command_result);
  BRACELET_ENSURE(command_result.Succeeded(),
                  "Configuring LLDB object map for sysroot %v failed: %s",
                  sysroot_abs, command_result.GetError());

  target.LoadCore(core_file.c_str(), error);
  BRACELET_TRY_CONTEXT(error, "Loading core file %v for executable %v", core_file,
                     exe);
  // Check that all the modules live inside the sysroot.
  const auto normalized_sysroot = sysroot_abs.lexically_normal();
  for (uint32_t i = 0; i < target.GetNumModules(); i++) {
    auto module = target.GetModuleAtIndex(i);
    if (!module.IsFileBacked())
      continue;
    const std::filesystem::path module_dir =
        module.GetFileSpec().GetDirectory();
    const auto normalized_module_dir = module_dir.lexically_normal();
    const auto mismatch =
        std::mismatch(normalized_sysroot.begin(), normalized_sysroot.end(),
                      normalized_module_dir.begin(), normalized_module_dir.end());
    BRACELET_ENSURE(mismatch.first == normalized_sysroot.end(),
                  "Module %v lives at %v outside the sysroot %v", module,
                  module_dir, sysroot_abs);
  }
  return std::make_unique<CoredumpObjectImpl>(target);
}
