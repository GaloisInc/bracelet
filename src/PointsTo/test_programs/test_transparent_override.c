#include "common.h"

bool which[] = {true, false, true, false};

static void foo(int *x) { printf("I am foo %p %d!\n", (void *)x, *x); }
static void bar(int *x) { printf("I am bar %p %d!\n", (void *)x, *x); }
static void __attribute__((noinline)) body() {
  void *null_ptr = NULL;
  // llvm wants to turn realloc into malloc.
  __asm__ __volatile__("" : "+r"(null_ptr));
  int *value = realloc(null_ptr, sizeof(int));
  for (size_t i = 0; i < sizeof(which); i++) {
    *value = i;
    (which[i] ? foo : bar)(value);
  }
  free(value);
}
MAIN(body)
