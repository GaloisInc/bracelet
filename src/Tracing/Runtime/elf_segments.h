#pragma once

#include "common.h"
#include <stdint.h>

namespace bracelet_trace {
namespace elf_segments {
// Does the given pointer live in an ELF-loaded segment?
bool pointerIsInElfSegment(uintptr_t);
// Rescan loaded dynamic libraries to re-populate the cache of ELF segments.
void rescan();
} // namespace elf_segments
} // namespace bracelet_trace
