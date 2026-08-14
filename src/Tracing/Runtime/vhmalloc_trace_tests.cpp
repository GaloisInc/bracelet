#include "vhmalloc.h"
#include <assert.h>
#include <inttypes.h>
#include <limits>
#include <set>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <tuple>
#include <vector>

static void *blackBox(void *X) {
  __asm__ __volatile__("" : "+r"(X));
  return X;
}

static void __attribute((noinline)) check_align(void) {
  for (size_t i = 1; i < 8192; i++) {
    void *ptr = blackBox(malloc(i));
    assert((((uintptr_t)ptr) % alignof(max_align_t)) == 0);
    free(ptr);
  }
}

static std::tuple<std::vector<void *>, std::vector<void *>> __attribute__((
    noinline))
check_malloc() {
  std::vector<void *> out_a;
  std::vector<void *> out_b;
  for (int i = 0; i < 12; i++) {
    out_a.push_back(malloc(12));
    out_b.push_back(malloc(13));
  }
  return std::make_tuple(std::move(out_a), std::move(out_b));
}

static void __attribute__((noinline)) test_allocas(void **a, void **b) {
  uint32_t foo[32];
  auto foo_info = vhmalloc::PointerInfo::of(foo);
  assert(foo_info);
  char baz[6];
  auto baz_info = vhmalloc::PointerInfo::of(baz);
  assert(baz_info);
  assert(baz_info.tag != foo_info.tag);
  *a = blackBox(reinterpret_cast<void *>(foo));
  *b = blackBox(reinterpret_cast<void *>(baz));
}

int main(void) {
  assert(!vhmalloc::PointerInfo::of(0));
  for (uint64_t x = 0; x < 8192; x++) {
    assert(
        !vhmalloc::PointerInfo::of(std::numeric_limits<uint64_t>::max() - x));
  }
  void *a;
  void *b;
  test_allocas(&a, &b);
  assert(!vhmalloc::PointerInfo::of(a));
  assert(!vhmalloc::PointerInfo::of(b));
  check_align();
  auto *ptr = reinterpret_cast<uint8_t *>(blackBox(malloc(8)));
  auto info = vhmalloc::PointerInfo::of(ptr);
  assert(info);
  assert(info.offset == 0);
  auto info2 = vhmalloc::PointerInfo::of(ptr + 1);
  assert(info.tag == info2.tag);
  assert(info2.offset == 1);
  free(ptr + 1);
  assert(!vhmalloc::PointerInfo::of(info));
  assert(!vhmalloc::PointerInfo::of(info + 1));
  assert(!vhmalloc::PointerInfo::of(0xdeadbeefULL));

  size_t malloc_sz = 8192;
  ptr = reinterpret_cast<uint8_t *>(blackBox(malloc(malloc_sz)));
  info = vhmalloc::PointerInfo::of(ptr);
  for (size_t i = 0; i < 1000000; i++) {
    info2 = vhmalloc::PointerInfo::of(ptr + i);
    if (i < malloc_sz) {
      assert(info.tag == info2.tag);
      assert(info2.offset == i);
    } else {
      assert(info2.tag != info.tag);
    }
  }

  auto [out_a, out_b] = check_malloc();
  std::set<vhmalloc::Tag> a_tags;
  std::set<vhmalloc::Tag> b_tags;
  for (void *ptr : out_a) {
    auto info = vhmalloc::PointerInfo::of(ptr);
    assert(info);
    a_tags.insert(info.tag);
  }
  for (void *ptr : out_b) {
    auto info = vhmalloc::PointerInfo::of(ptr);
    assert(info);
    b_tags.insert(info.tag);
  }
  assert(a_tags.size() == 1);
  assert(b_tags.size() == 1);
  vhmalloc::Tag a_tag = *a_tags.begin();
  vhmalloc::Tag b_tag = *b_tags.begin();
  assert(a_tag != b_tag);
  return 0;
}
