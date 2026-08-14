#include "../elf_segments.h"
#include <assert.h>
#include <dlfcn.h>
#include <memory>
#include <stdio.h>
#include <stdlib.h>

using namespace bracelet_trace::elf_segments;

int my_bss = 0;
int my_data = 75;
const int my_rodata = 7497312;
void my_func(void) { printf("Hello! My name is my_func.\n"); }

int main(int argc, const char *argv[]) {
  assert(argc == 2 && "USAGE: ./<exe> <test library>");
  assert(pointerIsInElfSegment(reinterpret_cast<uintptr_t>(&my_bss)));
  assert(pointerIsInElfSegment(reinterpret_cast<uintptr_t>(&my_data)));
  assert(pointerIsInElfSegment(reinterpret_cast<uintptr_t>(&my_rodata)));
  assert(pointerIsInElfSegment(reinterpret_cast<uintptr_t>(my_func)));
  int stack_var = 12;
  assert(!pointerIsInElfSegment(reinterpret_cast<uintptr_t>(&stack_var)));
  auto heap_var = std::make_unique<int>(37);
  assert(!pointerIsInElfSegment(reinterpret_cast<uintptr_t>(heap_var.get())));
  assert(!pointerIsInElfSegment(0x0));
  auto *lib = dlopen(argv[1], RTLD_LAZY);
  if (lib == nullptr) {
    fprintf(stderr, "dlopen(): %s", dlerror());
    abort();
  }
  assert(pointerIsInElfSegment(
      reinterpret_cast<uintptr_t>(dlsym(lib, "lib_bss"))));
  assert(pointerIsInElfSegment(
      reinterpret_cast<uintptr_t>(dlsym(lib, "lib_data"))));
  assert(pointerIsInElfSegment(
      reinterpret_cast<uintptr_t>(dlsym(lib, "lib_rodata"))));
  assert(pointerIsInElfSegment(
      reinterpret_cast<uintptr_t>(dlsym(lib, "lib_func"))));
  return 0;
}
