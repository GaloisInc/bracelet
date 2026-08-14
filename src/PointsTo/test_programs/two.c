// Test argument-less function (pointers)

#include "common.h"

bool which[] = {true, false, true, false};

static void foo() { printf("I am foo!\n"); }
static void bar() { printf("I am bar!\n"); }
static void __attribute__((noinline)) body() {
  for (size_t i = 0; i < sizeof(which); i++) {
    (which[i] ? foo : bar)();
  }
}
MAIN(body)
