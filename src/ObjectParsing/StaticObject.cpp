#include "ObjectParsing.h"
#include "Result/Result.h"
#include "Result/ResultLLVM.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Casting.h"
#include <cstdint>
#include <iomanip>
#include <llvm/Object/Binary.h>
#include <llvm/Object/ELFObjectFile.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Support/MemoryBuffer.h>
#include <map>
#include <tuple>

using namespace bracelet;
using namespace object_parsing;

namespace {

struct ExplodedAddress {
  uint32_t section_offset = 0;       // The offset within a section
  uint16_t section_idx_plus_one = 0; // We store section_idx + 1 so that no
                                     // valid address is NULL
  uint16_t zero = 0; // This is used in bracelet::Node for storing the local idx

  ExplodedAddress() {}
  ExplodedAddress(Address addr) { memcpy(this, &addr, sizeof(addr)); }
  operator Address() const {
    Address out;
    memcpy(&out, this, sizeof(out));
    return out;
  }

  operator bool() const { return section_idx_plus_one != 0; }

  uint16_t section_idx() const {
    assert(section_idx_plus_one >= 1);
    return section_idx_plus_one - 1;
  }
  void section_idx(uint16_t idx) {
    assert(idx < std::numeric_limits<uint16_t>::max());
    section_idx_plus_one = idx + 1;
  }
};
static_assert(sizeof(ExplodedAddress) == sizeof(Address), "");

class ExplodedAddressLoadInfo : public llvm::LoadedObjectInfo {
public:
  Result<uint64_t> translateAddr(Address addr) {
    ExplodedAddress exp;
    // We want a floor lookup: the section whose start address is the greatest
    // value <= addr (i.e. the section that contains addr). upper_bound(addr)
    // returns the first section whose start is strictly > addr, so the element
    // before it is that floor.
    auto mb_sec = sections.upper_bound(addr);
    BRACELET_ENSURE(mb_sec != sections.begin(),
                  "Should be able to find section for %x", addr);
    mb_sec--;
    auto sectionOff = addr - mb_sec->second.getAddress();
    BRACELET_ENSURE(
        sectionOff < mb_sec->second.getSize(),
        "Address of %x should be inbounds for section %lu of size %lu ", addr,
        mb_sec->second.getIndex(), mb_sec->second.getSize());
    exp.section_idx(mb_sec->second.getIndex());
    exp.section_offset = sectionOff;

    return exp;
  }

  virtual uint64_t
  getSectionLoadAddress(const llvm::object::SectionRef &Sec) const {
    ExplodedAddress addr;
    addr.section_idx(Sec.getIndex());
    return addr;
  }

  virtual std::unique_ptr<LoadedObjectInfo> clone() const {
    return std::make_unique<ExplodedAddressLoadInfo>(sections);
  }

  ExplodedAddressLoadInfo(
      const std::map<uint64_t, llvm::object::SectionRef> &sections)
      : sections(sections) {}

  ExplodedAddressLoadInfo(llvm::object::ObjectFile &obj) {
    for (auto sec : obj.sections()) {
      sections.insert({sec.getAddress(), sec});
    }
  }

private:
  std::map<uint64_t, llvm::object::SectionRef> sections;
};

struct StaticObject : public Object {
  StaticObject(std::unique_ptr<llvm::object::ObjectFile> obj,
               std::unique_ptr<llvm::MemoryBuffer> obj_buffer)
      : obj(std::move(obj)), obj_buffer(std::move(obj_buffer)) {}
  Result<void> init() {
    for (const auto &section : obj->sections()) {
      auto idx = section.getIndex();
      if (sections.size() <= idx + 1)
        sections.resize(idx + 1);
      BRACELET_ENSURE(!sections[idx], "Duplicate section idx %d", idx);
      Section s;
      s.name = BRACELET_TRY(section.getName());
      s.section = section;
      sections[idx] = s;
    }
    extern_symbols_section = sections.size();
    Section s;
    s.name = "extern symbols section (bracelet)";
    sections.push_back(s);
    return bracelet::ok();
  }
  virtual ~StaticObject() {}
  virtual bool isApple() override {
    return obj->getOS() == llvm::Triple::OSType::MacOSX ||
           obj->getOS() == llvm::Triple::OSType::Darwin;
  }
  // The Data& shouldn't be used after the callback returns.
  virtual Result<void> visitSections(
      std::function<Result<void>(const object_parsing::Section &)> callback)
      override {
    for (size_t s_idx = 0; s_idx < sections.size(); s_idx++) {
      if (s_idx == extern_symbols_section)
        continue;
      if (auto &s = sections[s_idx]) {
        ExplodedAddress start;
        start.section_idx(s_idx);
        BRACELET_TRY(callback(object_parsing::Section{
            .name = s->name,
            .start = start,
            .size = s->section.getSize(),
        }));
      }
    }
    return bracelet::ok();
  }

  virtual bool SupportsFastAddrNodeMap() override { return true; }

  virtual Result<void> visitDwarfContexts(
      std::function<Result<void>(std::unique_ptr<llvm::DWARFContext>,
                                 Object::AddressTranslation)>
          visit) override {
    auto loadInfo = ExplodedAddressLoadInfo(*obj);
    auto dwf = llvm::DWARFContext::create(
        *obj, llvm::DWARFContext::ProcessDebugRelocations::Process, &loadInfo);
    return visit(std::move(dwf), [&loadInfo](auto addr) {
      return loadInfo.translateAddr(addr);
    });
  }

  virtual Result<const std::string_view> symbolName(Address addr) override {
    auto iter = symbol_names.find(addr);
    BRACELET_ENSURE(iter != symbol_names.end(),
                  "Cannot find symbol name for address %x", addr);
    return iter->second;
  }
  virtual Result<void> copyData(boost::span<uint8_t> dst,
                                Address addr_raw) override {
    if (dst.empty())
      return bracelet::ok();
    BRACELET_ENSURE(addr_raw, "Can't copy from NULL");
    ExplodedAddress addr = addr_raw;
    BRACELET_ENSURE(addr.section_idx() < sections.size(),
                  "Address section is out of range");
    auto &section = sections[addr.section_idx()];
    BRACELET_ENSURE(section, "section %d is undefined", addr.section_idx());
    auto section_body = BRACELET_TRY(section->contents());
    BRACELET_ENSURE(addr.section_offset <= section_body.size(),
                  "Section offset is out of bounds for section");
    auto address_data = section_body.substr(addr.section_offset);
    BRACELET_ENSURE(dst.size() <= address_data.size(),
                  "Can only copy %d bytes. %d bytes desired.",
                  address_data.size(), dst.size());
    memcpy(dst.data(), address_data.data(), dst.size());
    return bracelet::ok();
  }
  virtual Result<void> resolvePointers(boost::span<Address> dst,
                                       Address addr_raw) override {
    size_t sizeof_pointer = 8;
    assert(sizeof_pointer == obj->getBytesInAddress());
    if (dst.empty())
      return bracelet::ok();
    BRACELET_ENSURE(addr_raw, "Can't copy from NULL");
    ExplodedAddress addr = addr_raw;
    BRACELET_ENSURE(addr.section_idx() < sections.size(),
                  "Address section is out of range");
    auto &section = sections[addr.section_idx()];
    assert(section);
    BRACELET_ENSURE(addr.section_offset <= section->section.getSize(),
                  "Section offset is out of bounds for section");
    BRACELET_ENSURE(
        dst.size() <=
            (section->section.getSize() - addr.section_offset) / sizeof_pointer,
        "Can only copy %d addresses. %d addresses desired.",
        (section->section.getSize() - addr.section_offset) / sizeof_pointer,
        dst.size());
    auto base_address = section->section.getAddress() + addr.section_offset;
    auto *relocations = BRACELET_TRY(section->relocations(this));
    // A NOBITS section (e.g. .bss) has no on-disk contents and is zero-filled
    // at load time. A pointer-sized slot there with no relocation therefore
    // holds NULL when the program starts. This is the static-object analogue of
    // reading that slot out of a coredump (which would observe 0 unless the
    // program had written to it). Concretely, the head pointer of the dlsym
    // page list lives in .bss and is NULL until the runtime populates it, so
    // resolving it must yield 0 rather than failing.
    bool is_nobits = section->section.isBSS();
    for (size_t i = 0; i < dst.size(); i++) {
      auto address = base_address + i * sizeof_pointer;
      auto reloc = relocations->find(address);
      if (reloc == relocations->end()) {
        BRACELET_ENSURE(is_nobits,
                      "Cannot find relocation for section %s, address %d",
                      section->name, address);
        dst[i] = 0;
        continue;
      }
      dst[i] = reloc->second;
    }
    return bracelet::ok();
  }

private:
  struct Section {
    Section() {}
    std::string name;
    llvm::object::SectionRef section;
    // We lazily load the contents
    std::optional<std::string_view> lazy_contents;
    // A map from offset to address.
    std::optional<absl::flat_hash_map<uint64_t, Address>> lazy_relocations;

    Result<absl::flat_hash_map<uint64_t, Address> *>
    relocations(StaticObject *so) {
      if (!lazy_relocations) {
        absl::flat_hash_map<uint64_t, Address> relocations;
        auto &obj = so->obj;
        for (const auto &reloc_section : obj->sections()) {
          auto relocation_target =
              BRACELET_TRY(reloc_section.getRelocatedSection());
          if (relocation_target == obj->section_end() ||
              relocation_target->getIndex() != section.getIndex()) {
            continue;
          }
          for (const auto &reloc : reloc_section.relocations()) {
            auto addr = reloc.getOffset();
            assert(!relocations.contains(addr));
            auto symbol = reloc.getSymbol();
            if (symbol == obj->symbol_end())
              continue;
            auto symbol_addr = BRACELET_TRY(symbol->getAddress());
            auto symbol_section = BRACELET_TRY(symbol->getSection());
            ExplodedAddress out;
            // TODO: support offsets into symbols (e.g. &my_extern[74]
            // shouldn't be the same as &my_extern[764])
            if (symbol_section == so->obj->section_end()) {
              // it's an extern symbol
              auto symbol_name = BRACELET_TRY(symbol->getName());
              out.section_idx(so->extern_symbols_section);
              auto iter = so->extern_symbol_indices.find(symbol_name);
              if (iter != so->extern_symbol_indices.end())
                out.section_offset = iter->second;
              else {
                out.section_offset = so->extern_symbol_indices.size();
                so->extern_symbol_indices[symbol_name] = out.section_offset;
                CHECK(!so->symbol_names.contains(out));
                so->symbol_names[out] = symbol_name;
              }
            } else {
              auto elfreloc =
                  static_cast<llvm::object::ELFRelocationRef>(reloc);
              auto mbreladd = elfreloc.getAddend();
              auto reladd = mbreladd ? *mbreladd : 0;
              out.section_idx(symbol_section->getIndex());
              out.section_offset =
                  (symbol_addr - symbol_section->getAddress()) + reladd;
              auto section_size = symbol_section->getSize();
              CHECK(out.section_offset <= section_size)
                  << "Section error: " << name << ": " << out.section_offset
                  << "; section_size= " << section_size << "; symbol="
                  << std::quoted(
                         std::string_view(BRACELET_TRY(symbol->getName())))
                  << "; symbol_section="
                  << std::quoted(std::string_view(
                         BRACELET_TRY(symbol_section->getName())))
                  << "; symbol_addr=" << symbol_addr
                  << "; symbol_value=" << BRACELET_TRY(symbol->getValue())
                  << "; section_start=" << symbol_section->getAddress();
              if (!so->symbol_names.contains(out))
                so->symbol_names[out] = BRACELET_TRY(symbol->getName());
            }
            relocations[addr] = out;
          }
        }
        lazy_relocations = std::move(relocations);
      }
      return &*lazy_relocations;
    }

    Result<std::string_view> contents() {
      if (!lazy_contents) {
        auto contents = BRACELET_TRY_CONTEXT(
            section.getContents(), "Loading contents of section %s", name);
        assert(contents.size() == section.getSize());
        lazy_contents = contents;
      }
      return *lazy_contents;
    }
  };

  std::unique_ptr<llvm::object::ObjectFile> obj;
  std::unique_ptr<llvm::MemoryBuffer> obj_buffer;
  // Sections indexed by SectionRef.getIndex()
  std::vector<std::optional<Section>> sections;
  unsigned extern_symbols_section;
  llvm::StringMap<unsigned> extern_symbol_indices;
  absl::flat_hash_map<Address, std::string_view> symbol_names;
};
} // namespace

Result<std::tuple<std::unique_ptr<llvm::object::ObjectFile>,
                  std::unique_ptr<llvm::MemoryBuffer>>>
object_parsing::openLLVMObject(const std::filesystem::path &exe) {
  auto bin = BRACELET_TRY_CONTEXT(llvm::object::createBinary(exe.c_str()),
                                "Opening binary %s", exe.c_str());
  BRACELET_ENSURE(llvm::dyn_cast<llvm::object::ObjectFile>(bin.getBinary()),
                "Exe %s isn't an object file", exe.c_str());
  auto [orig_bin_ptr, buf_ptr] = bin.takeBinary();
  return std::make_tuple<std::unique_ptr<llvm::object::ObjectFile>,
                         std::unique_ptr<llvm::MemoryBuffer>>(
      std::unique_ptr<llvm::object::ObjectFile>(
          static_cast<llvm::object::ObjectFile *>(orig_bin_ptr.release())),
      std::move(buf_ptr));
  ;
}

Result<std::unique_ptr<Object>>
object_parsing::openObject(const std::filesystem::path &exe) {
  auto [llObj, buf] = BRACELET_TRY(openLLVMObject(exe));
  auto out = std::make_unique<StaticObject>(std::move(llObj), std::move(buf));
  BRACELET_TRY_CONTEXT(out->init(), "Reading binary %s", exe.c_str());
  return out;
}
