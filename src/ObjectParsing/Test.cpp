// Check that test case objets, executables, and coredumps match what we expect.

#include "ObjectParsing.h"
#include "Subprocess/Subprocess.h"
#include "absl/container/flat_hash_map.h"
#include "tempfile/tempfile.h"
#include <algorithm>
#include <array>
#include <boost/icl/interval_map.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <filesystem>
#include <functional>
#include <map>
#include <sys/wait.h>
#include <utility>
#include <vector>

namespace {
std::filesystem::path current_exe() {
  // TODO: on macos use _NSGetExecutablePath()
  return std::filesystem::read_symlink("/proc/self/exe");
}

// If the input looks like "foo@VERSION", just return "foo".
std::string_view versionLessFunctionName(std::string_view name) {
  return name.substr(0, name.find('@'));
}

void test1(bracelet::object_parsing::Object *obj) {
  boost::icl::interval_map<bracelet::object_parsing::Address, std::string>
      section_names;
  auto find_section = [&](bracelet::object_parsing::Address addr) -> std::string {
    auto iter = section_names.find(addr);
    REQUIRE(iter != section_names.end());
    return iter->second;
  };
  absl::flat_hash_map<std::string, bracelet::object_parsing::Address>
      section_starts;
  bracelet::unwrap(
      obj->visitSections([&](const bracelet::object_parsing::Section &section)
                             -> bracelet::Result<void> {
        section_names.add(std::make_pair(
            boost::icl::interval<bracelet::object_parsing::Address>::right_open(
                section.start, section.start + section.size),
            std::string(section.name)));
        section_starts[section.name] = section.start;
        return bracelet::ok();
      }));
  bool saw_test_section = false;
  bracelet::unwrap(obj->visitSections([&](const bracelet::object_parsing::Section
                                            &section) -> bracelet::Result<void> {
    if (section.name != "test_section") {
      return bracelet::ok();
    }
    saw_test_section = true;
    REQUIRE(section.size == 3 * sizeof(uint64_t));
    uint64_t value[3];
    bracelet::unwrap(obj->copyData(
        boost::span(reinterpret_cast<uint8_t *>(&value), sizeof(value)),
        section.start));
    CHECK(value[0] == 123);
    CHECK(value[2] == 456);
    uint64_t x = 0;
    bracelet::unwrap(
        obj->copyData(boost::span(reinterpret_cast<uint8_t *>(&x), sizeof(x)),
                      section.start));
    CHECK(x == 123);
    bracelet::unwrap(
        obj->copyData(boost::span(reinterpret_cast<uint8_t *>(&x), sizeof(x)),
                      section.start + 16));
    CHECK(x == 456);
    bracelet::object_parsing::Address addr;
    bracelet::unwrap(
        obj->resolvePointers(boost::span(&addr, 1), section.start + 8));
    CHECK(bracelet::unwrap(obj->symbolName(addr)) == "foo");
    CHECK(find_section(addr) == ".data");
    return bracelet::ok();
  }));
  REQUIRE(saw_test_section);
  std::array<bracelet::object_parsing::Address, 3> my_pointers;
  std::array<uint64_t, 3> canonical_offsets = {1, 4, 75};
  bracelet::unwrap(obj->resolvePointers(
      boost::span(my_pointers), section_starts["my_arr_pointers_section"]));
  for (size_t i = 0; i < my_pointers.size(); i++) {
    CHECK((my_pointers[i] ==
               section_starts["my_arr_section"] + canonical_offsets[i] * 4 ||
           // TODO: remove this when we support offsets into symbols for the
           // object reader
           my_pointers[i] == section_starts["my_arr_section"]));
    CHECK(find_section(my_pointers[i]) == "my_arr_section");
    CHECK(bracelet::unwrap(obj->symbolName(my_pointers[i])) == "my_arr");
  }

  std::array<bracelet::object_parsing::Address, 2> int_addrs;
  bracelet::unwrap(obj->resolvePointers(boost::span(int_addrs),
                                      section_starts["int_pointers"]));
  CHECK(bracelet::unwrap(obj->symbolName(int_addrs[0])) == "ethan");
  CHECK(bracelet::unwrap(obj->symbolName(int_addrs[1])) == "baz");

  std::array<bracelet::object_parsing::Address, 3> function_addrs;
  bracelet::unwrap(obj->resolvePointers(boost::span(function_addrs),
                                      section_starts["function_pointers"]));
  CHECK(versionLessFunctionName(
            bracelet::unwrap(obj->symbolName(function_addrs[0]))) == "perror");
  CHECK(versionLessFunctionName(
            bracelet::unwrap(obj->symbolName(function_addrs[1]))) == "abort");
  CHECK(versionLessFunctionName(
            bracelet::unwrap(obj->symbolName(function_addrs[2]))) == "exit");
}

// Regression test for following a pointer whose target lives in .bss.
//
// .bss is a NOBITS (zero-initialized) section, so the linker emits no
// relocation for storage there: at load time the slot is simply NULL. This is
// exactly the shape of the dlsym page-list head pointer that bracelet-edges
// follows. Resolving a pointer out of a .bss slot with no relocation should
// yield NULL (0), not error out.
void test3(bracelet::object_parsing::Object *obj) {
  bool saw_holder = false;
  bracelet::unwrap(
      obj->visitSections([&](const bracelet::object_parsing::Section &s)
                             -> bracelet::Result<void> {
        if (s.name != "bss_pointer_holder") {
          return bracelet::ok();
        }
        saw_holder = true;
        // The holder slot points at the .bss global `bss_pointer`.
        bracelet::object_parsing::Address bss_addr;
        bracelet::unwrap(obj->resolvePointers(boost::span(&bss_addr, 1), s.start));
        CHECK(bracelet::unwrap(obj->symbolName(bss_addr)) == "bss_pointer");
        // The .bss slot itself has no relocation; its load-time value is NULL.
        bracelet::object_parsing::Address value = 0xdeadbeef;
        bracelet::unwrap(obj->resolvePointers(boost::span(&value, 1), bss_addr));
        CHECK(value == 0);
        return bracelet::ok();
      }));
  CHECK(saw_holder);
}

// Regression test for translating an address that lands exactly on a section
// boundary.
//
// bracelet-edges translates DWARF addresses (e.g. a function low_pc) into
// (section, offset) pairs via the AddressTranslation callback handed to
// visitDwarfContexts. A function at the very start of .text has a low_pc equal
// to .text's start address, so the translation is routinely asked to resolve an
// address that sits exactly on a section boundary. It must map such an address
// to offset 0 within the section that *starts* there -- not to the previous
// section. The off-by-one that this guards against (lower_bound + decrement)
// crashed with "Address of ... should be inbounds for section ... of size ...".
void testSectionBoundaryTranslation(const std::filesystem::path &exe) {
  // Raw section load addresses come straight from the LLVM object; these are
  // the addresses the DWARF translation is fed in real runs.
  // Collect section start addresses, keyed by address so we can tell which
  // addresses are unambiguous. The translator maps address -> section via a
  // std::map keyed on section start; when several sections share a start (e.g.
  // a zero-size .tm_clone_table sitting on top of the next real section) which
  // one wins is an insertion-order detail unrelated to this bug. Probe only
  // addresses owned by exactly one nonzero-size section so the expectation is
  // well defined -- this still includes the .text/.plt.got boundary that the
  // crashing binaries choke on.
  auto [llobj, buf] =
      bracelet::unwrap(bracelet::object_parsing::openLLVMObject(exe));
  std::map<uint64_t, int> starts; // address -> number of sections starting here
  for (const auto &sec : llobj->sections()) {
    uint64_t addr = sec.getAddress();
    if (addr != 0)
      starts[addr]++;
  }
  std::vector<uint64_t> section_starts;
  for (auto [addr, count] : starts)
    if (count == 1)
      section_starts.push_back(addr);
  REQUIRE(!section_starts.empty());

  auto obj = bracelet::unwrap(bracelet::object_parsing::openObject(exe));
  bool ran = false;
  bracelet::unwrap(obj->visitDwarfContexts(
      [&](std::unique_ptr<llvm::DWARFContext>,
          bracelet::object_parsing::Object::AddressTranslation translate)
          -> bracelet::Result<void> {
        ran = true;
        for (uint64_t start : section_starts) {
          // Translating a section-start address must succeed and yield offset 0
          // within the section that *starts* there. The exploded address keeps
          // the section offset in its low 32 bits, so offset 0 confirms the
          // boundary address resolved to that section -- not the previous one.
          // The pre-fix off-by-one either errored ("Address ... should be
          // inbounds ...") or resolved to the previous section (nonzero
          // offset).
          bracelet::object_parsing::Address at_start =
              bracelet::unwrap(translate(start));
          CHECK((at_start & 0xffffffffULL) == 0);
        }
        return bracelet::ok();
      }));
  CHECK(ran);
}

void test2(bracelet::object_parsing::Object *obj) {
  // Check that we're getting the mangled names.
  bool saw_ptr_section = false;
  bracelet::unwrap(
      obj->visitSections([&](const bracelet::object_parsing::Section &s) {
        if (s.name == "my_ptr") {
          saw_ptr_section = true;
          bracelet::object_parsing::Address addr;
          bracelet::unwrap(obj->resolvePointers(boost::span(&addr, 1), s.start));
          std::string name(bracelet::unwrap(obj->symbolName(addr)));
          CHECK(name == "_Z3fooiPKc");
        }
        return bracelet::ok();
      }));
  CHECK(saw_ptr_section);
}

} // namespace

TEST_CASE("test1.o") {
  auto obj = bracelet::unwrap(bracelet::object_parsing::openObject(
      current_exe().parent_path() /
      "object_parsing_testcase1.p/test-cases_test1.c.o"));
  test1(obj.get());
}

TEST_CASE("test1.core") {
  auto tmp = bracelet::unwrap(bracelet::tempfile::TemporaryDirectory::create());
  auto exe = current_exe().parent_path() / "object_parsing_testcase1";
  REQUIRE(bracelet::unwrap(bracelet::subprocess::call(
              exe.c_str(), boost::span<std::string_view>(), &tmp.path())) == 0);
  std::vector<std::filesystem::path> elems;
  for (const auto &dentry : std::filesystem::directory_iterator(tmp.path()))
    elems.emplace_back(dentry.path());
  REQUIRE(elems.size() == 1);
  std::filesystem::path corefile = elems[0];
  auto obj =
      bracelet::unwrap(bracelet::object_parsing::openCore(exe, "/", corefile));
  test1(obj.get());
}

TEST_CASE("test1.exe") {
  auto obj = bracelet::unwrap(bracelet::object_parsing::openObject(
      current_exe().parent_path() / "object_parsing_testcase1"));
  test1(obj.get());
}

TEST_CASE("test3.exe") {
  auto obj = bracelet::unwrap(bracelet::object_parsing::openObject(
      current_exe().parent_path() / "object_parsing_testcase3"));
  test3(obj.get());
}

TEST_CASE("test3.core") {
  auto tmp = bracelet::unwrap(bracelet::tempfile::TemporaryDirectory::create());
  auto exe = current_exe().parent_path() / "object_parsing_testcase3";
  REQUIRE(bracelet::unwrap(bracelet::subprocess::call(
              exe.c_str(), boost::span<std::string_view>(), &tmp.path())) == 0);
  std::vector<std::filesystem::path> elems;
  for (const auto &dentry : std::filesystem::directory_iterator(tmp.path()))
    elems.emplace_back(dentry.path());
  REQUIRE(elems.size() == 1);
  std::filesystem::path corefile = elems[0];
  auto obj =
      bracelet::unwrap(bracelet::object_parsing::openCore(exe, "/", corefile));
  test3(obj.get());
}

TEST_CASE("section boundary translation.exe") {
  testSectionBoundaryTranslation(current_exe().parent_path() /
                                 "object_parsing_testcase1");
}

TEST_CASE("test2.o") {
  auto obj = bracelet::unwrap(bracelet::object_parsing::openObject(
      current_exe().parent_path() /
      "object_parsing_testcase2.p/test-cases_test2.cpp.o"));
  test2(obj.get());
}

TEST_CASE("test2.core") {
  auto tmp = bracelet::unwrap(bracelet::tempfile::TemporaryDirectory::create());
  auto exe = current_exe().parent_path() / "object_parsing_testcase2";
  REQUIRE(bracelet::unwrap(bracelet::subprocess::call(
              exe.c_str(), boost::span<std::string_view>(), &tmp.path())) == 0);
  std::vector<std::filesystem::path> elems;
  for (const auto &dentry : std::filesystem::directory_iterator(tmp.path()))
    elems.emplace_back(dentry.path());
  REQUIRE(elems.size() == 1);
  std::filesystem::path corefile = elems[0];
  auto obj =
      bracelet::unwrap(bracelet::object_parsing::openCore(exe, "/", corefile));
  test2(obj.get());
}

TEST_CASE("test2.exe") {
  auto obj = bracelet::unwrap(bracelet::object_parsing::openObject(
      current_exe().parent_path() / "object_parsing_testcase2"));
  test2(obj.get());
}
