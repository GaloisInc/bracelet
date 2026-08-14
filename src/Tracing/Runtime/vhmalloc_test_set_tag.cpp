#include "vhmalloc.h"
#include <assert.h>
#include <limits>
#include <stdlib.h>

int main() {
  auto *ptr = malloc(13);
  auto info = vhmalloc::PointerInfo::of(ptr);
  assert(info.tag == std::numeric_limits<uint64_t>::max());
  vhmalloc::setTag(ptr, 1);
  info = vhmalloc::PointerInfo::of(ptr);
  assert(info.tag == 1);

  constexpr size_t NUM_ALLOCS = 8192;
  static void *pointers[NUM_ALLOCS];
  static size_t tags[NUM_ALLOCS];
  for (size_t i = 0; i < NUM_ALLOCS; i++) {
    pointers[i] = malloc(8);
    auto info = vhmalloc::PointerInfo::of(pointers[i]);
    assert(info.tag == std::numeric_limits<uint64_t>::max());
    vhmalloc::setTag(pointers[i], i + 2);
    tags[i] = i + 2;
    for (size_t j = 0; j <= i; j++) {
      info = vhmalloc::PointerInfo::of(pointers[j]);
      assert(info.tag == tags[j]);
    }
  }
  // TODO: check that this works with freeing
  return 0;
}
