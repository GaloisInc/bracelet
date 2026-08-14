#pragma once

#include <stdint.h>

namespace vhmalloc {

// Tag is a non-zero value associated with the allocation.
using Tag = uint64_t;

struct PointerInfo {
  Tag tag;
  uint64_t offset;

  // Return the PointerInfo associated with a given addresss (if applicable).
  //
  // If ptr wasn't allocated via vhmalloc, then allocation_id and offset will be
  // zero.
  static PointerInfo of(uintptr_t);
  template <typename T> static PointerInfo of(T *t) {
    return of(reinterpret_cast<uintptr_t>(t));
  }
  template <typename T> static PointerInfo of(const T *t) {
    return of(reinterpret_cast<uintptr_t>(t));
  }
  operator bool() const { return tag != 0; }
  bool operator==(const PointerInfo &x) const {
    return tag == x.tag && offset == x.offset;
  }
  bool operator!=(const PointerInfo &x) const { return !(*this == x); }
};

// Panics if ptr isn't a vhmalloc-allocated pointer or if tag is zero, or if the
// tag had previously been set.
void setTag(void *ptr, Tag tag);
} // namespace vhmalloc
