#include "common.h"
#include <string.h>

bool which[] = {true, false, true, false};

static size_t foo(const char *x) {
  printf("Hello! %s\n", x);
  return 13;
}
static void __attribute__((noinline)) body() {
  for (size_t i = 0; i < sizeof(which); i++) {
    size_t out = (which[i] ? foo : strlen)("Mr. Salamander");
    printf("%d\n", (int)out);
  }
}
MAIN(body)
