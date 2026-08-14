#include <atomic>
#include <dlfcn.h>
#include <stdlib.h>

#include "BraceletRuntimeStructs_c.h"
#include "common.h"
#include "elf_segments.h"
#include "trace_runtime.h"
#include "vhmalloc.h"

// Note that Heap-Layers also exports some malloc definitions.

extern "C" EXPORT void braceletTraceBuffer(BraceletTraceSite *site, const void *ptr,
                                         size_t size) {
  bracelet_trace::traceBuffer(
      site, std::string_view(reinterpret_cast<const char *>(ptr), size));
}
extern "C" EXPORT void braceletTraceWord(BraceletTraceSite *site, uintptr_t value) {
  bracelet_trace::traceWord(site, value);
}
extern "C" EXPORT void braceletTraceTagAllocation(const BraceletTraceSite *site,
                                                void *ptr) {
  vhmalloc::Tag tag = reinterpret_cast<uint64_t>(site);
  vhmalloc::setTag(ptr, tag);
}
extern "C" EXPORT void *braceletTraceAllocaAllocate(const BraceletTraceSite *site,
                                                  uint64_t size) {
  auto *ptr = malloc(size);
  braceletTraceTagAllocation(site, ptr);
  return ptr;
}
extern "C" EXPORT void braceletTraceAllocaFree(void *ptr) { free(ptr); }

// Defined in Heap-Layers
extern "C" void *my_dlsym(void *, const char *);
extern "C" EXPORT void *dlopen(const char *path, int flags) {
  static std::atomic<void *(*)(const char *, int)> underlying_atomic = nullptr;
  static_assert(decltype(underlying_atomic)::is_always_lock_free, "");
  auto underlying = underlying_atomic.load(std::memory_order_relaxed);
  if (underlying == nullptr) {
    underlying = reinterpret_cast<void *(*)(const char *, int)>(
        my_dlsym(RTLD_NEXT, "dlopen"));
    // All invocations of dlsym() should return the same result, so it's fine to
    // overwrite any old value if we race.
    underlying_atomic.store(underlying, std::memory_order_relaxed);
  }
  auto rc = underlying(path, flags);
  bracelet_trace::elf_segments::rescan();
  return rc;
}

extern "C" EXPORT int dlclose(void *) {
  // We want to disable dlclose() so that ELF segments have unique addresses.
  // dlclose() frequently doesn't actually close the library anyway:
  // https://kishoreganesh.com/post/why-dl-close-did-not-work/
  return 0;
}
