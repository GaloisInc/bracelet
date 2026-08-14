// A simple test

#include "common.h"

#include <malloc.h>
#include <stdlib.h>
#include <string.h>

bool which[] = {true, false, true, false};

static void foo(int x) { printf("I am foo %d!\n", x); }
static void bar(int x) { printf("I am bar %d!\n", x); }
static void __attribute__((noinline)) body() {
  for (size_t i = 0; i < sizeof(which); i++) {
    (which[i] ? foo : bar)(i);
  }
  char *foo = malloc(8);
  strcpy(foo, "hello");
  char *bar = memalign(8, 8);
  char *baz;
  posix_memalign((void**)&baz, 8, 8);
  memcpy(bar, foo, 8);
  memcpy(baz, bar, 8);
  printf("%s\n", baz);
}
MAIN(body)
