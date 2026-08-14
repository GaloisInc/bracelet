// Test argument-less function with void (pointers)

#include "common.h"

bool which[] = {true, false, true, false};

static void foo(void) { printf("I am foo!\n"); }
static void bar(void) { printf("I am bar!\n"); }
static void __attribute__((noinline)) body() {
  for (size_t i = 0; i < sizeof(which); i++) {
    (which[i] ? foo : bar)();
  }
}
MAIN(body)
