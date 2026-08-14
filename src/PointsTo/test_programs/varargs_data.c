// Test argument-less function (pointers)

#include "common.h"

#include <stdarg.h>

static void __attribute__((noinline)) calls(int count, ...) {
  va_list args; // Declare a va_list variable
  // Initialize va_list to point to the first variable argument.
  // 'count' is the last fixed argument.
  va_start(args, count);

  void *data = va_arg(args, void *);
  printf("data = %p\n", data);

  // Clean up the va_list
  va_end(args);
}

static void __attribute__((noinline)) body() {
  void *baz = malloc(8);
  calls(1, baz);
}
MAIN(body)
