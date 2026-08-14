#define _GNU_SOURCE 1

#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "BraceletRuntimeStructs_c.h"
#include "edge_hash_set.h"
#include "elf_segments.h"
#include "trace_runtime.h"
#include "vhmalloc.h"

namespace {
thread_local bracelet_trace::EdgeHashSet TL_BUFFER(getenv("BRACELET_TRACE_DIR"),
                                                 10);
// Returns 0 if not valid
uintptr_t value_to_maybe_node(uintptr_t value) {
  if (bracelet_trace::elf_segments::pointerIsInElfSegment(value))
    return value;
  auto info = vhmalloc::PointerInfo::of(value);
  if (info)
    return info.tag;
  return 0;
}
} // namespace

void bracelet_trace::traceBuffer(BraceletTraceSite *site, std::string_view buffer) {
  // We assume pointers are aligned
  for (size_t idx = 0; idx + sizeof(uintptr_t) < buffer.size();
       idx += sizeof(uintptr_t)) {
    uintptr_t value;
    memcpy(&value, &buffer[idx], sizeof(value));
    bracelet_trace::traceWord(site, value);
  }
}
void bracelet_trace::traceWord(BraceletTraceSite *site, uintptr_t value) {
  value = value_to_maybe_node(value);
  if (value != 0)
    TL_BUFFER.add_edge({reinterpret_cast<uintptr_t>(site), value});
}
