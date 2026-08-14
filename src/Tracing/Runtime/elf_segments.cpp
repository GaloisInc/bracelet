#include <algorithm>
#include <array>
#include <elf.h>
#define GNU_SOURCE 1
#define BOOST_ICL_USE_STATIC_BOUNDED_INTERVALS 1

#include <atomic>
#include <boost/hash2/xxh3.hpp>
#include <boost/icl/interval_set.hpp>
#include <link.h>
#include <memory>

#include "elf_segments.h"

namespace {
using boost::icl::interval_set;
using AddrInterval = boost::icl::interval<uintptr_t>;

template <typename Cb> void cpp_iterate_phdr(Cb cb) {
  dl_iterate_phdr(
      [](dl_phdr_info *info, size_t, void *userptr) {
        Cb *cb = reinterpret_cast<Cb *>(userptr);
        (*cb)(info);
        return 0;
      },
      &cb);
}

struct ElfSegments {
  ElfSegments() {}

  interval_set<uintptr_t> segments;
  boost::hash2::xxh3_128::result_type segments_hash;
};

// We'll rarely update this interval set. As a result, rather than protecting
// this with a RwLock, we will CAS this pointer to update it and then _leak_ the
// old pointer. This lets us avoid dealing with reclamation techniques.
std::atomic<ElfSegments *> ELF_SEGMENTS = nullptr;
} // namespace

EXPORT bool bracelet_trace::elf_segments::pointerIsInElfSegment(uintptr_t ptr) {
  // Boost ICL will trip an assertion if we give it the max u64 value
  if (__builtin_expect(ptr == std::numeric_limits<uintptr_t>::max(), false))
    return false;
  auto *elf_segments = ELF_SEGMENTS.load(std::memory_order_acquire);
  if (__builtin_expect(elf_segments == nullptr, false)) {
    bracelet_trace::elf_segments::rescan();
    elf_segments = ELF_SEGMENTS.load(std::memory_order_acquire);
    assert(elf_segments != nullptr);
  }
  return elf_segments->segments.find(ptr) != elf_segments->segments.end();
}

void bracelet_trace::elf_segments::rescan() {
  ElfSegments *old = ELF_SEGMENTS.load();
  while (true) {
    auto new_intervals = std::make_unique<ElfSegments>();
    std::vector<AddrInterval::interval_type> intervals;
    cpp_iterate_phdr([&](dl_phdr_info *info) {
      for (size_t i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type != PT_LOAD)
          continue;
        uintptr_t base = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
        uintptr_t end = base + info->dlpi_phdr[i].p_memsz;
        assert(base < end);
        assert(base != std::numeric_limits<uintptr_t>::max());
        assert(end != std::numeric_limits<uintptr_t>::max());
        auto interval = AddrInterval::right_open(base, end);
        new_intervals->segments.add(interval);
        intervals.push_back(interval);
      }
    });
    std::sort(intervals.begin(), intervals.end());
    boost::hash2::xxh3_128 hasher(0);
    for (const auto &interval : intervals) {
      std::array<uint8_t, sizeof(uintptr_t)> bytes;
      uintptr_t value = interval.lower();
      memcpy(&bytes[0], &value, sizeof(uintptr_t));
      hasher.update(&bytes[0], bytes.size());
      value = interval.upper();
      memcpy(&bytes[0], &value, sizeof(uintptr_t));
      hasher.update(&bytes[0], bytes.size());
    }
    new_intervals->segments_hash = hasher.result();
    if (old && new_intervals->segments_hash == old->segments_hash) {
      // The sets are the same. No need to leak more memory.
      return;
    }
    if (ELF_SEGMENTS.compare_exchange_strong(old, new_intervals.get())) {
      // We were successful!
      // Don't free new_intervals when we exit.
      // Readers may still hold the old value, so intervals live until exit.
      (void)new_intervals.release(); // NOLINT(bugprone-unused-return-value)
      // We leak the old value.
      return;
    }
  }
}
