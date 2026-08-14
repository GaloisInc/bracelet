// A simple test

#include "common.h"

bool which[] = {true, false, true, false};

static void foo(int x) { printf("I am foo %d!\n", x); }
static void bar(int x) { printf("I am bar %d!\n", x); }
static void __attribute__((noinline)) body() {
  for (size_t i = 0; i < sizeof(which); i++) {
    (which[i] ? foo : bar)(i);
  }
}
MAIN(body)
