// Test argument-less function (pointers)

#include "common.h"

#include <stdarg.h>

static void foo() { printf("I am foo!\n"); }
static void bar() { printf("I am bar!\n"); }
static void baz() { printf("I am baz!\n"); }

static void* launder = NULL;

static void __attribute__((noinline)) calls(int count, ...) {
  va_list args; // Declare a va_list variable
  // Initialize va_list to point to the first variable argument.
  // 'count' is the last fixed argument.
  va_start(args, count);

  void (*fn)() = va_arg(args, void (*)());
  fn();

  // Clean up the va_list
  va_end(args);
}

static void __attribute__((noinline)) body() {
  launder = (void*)foo;
  calls(1, launder);
  calls(1, bar);
}
MAIN(body)
