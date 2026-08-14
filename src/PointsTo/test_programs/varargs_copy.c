// Test argument-less function (pointers)

#include "common.h"

#include <stdarg.h>

static void __attribute__((noinline)) calls(int count, ...) {
  va_list args1, args2;
  va_start(args1, count);
  va_copy(args2, args1);

  void *data1 = va_arg(args1, void *);
  printf("data1 = %p\n", data1);

  void *data2 = va_arg(args2, void *);
  printf("data2 = %p\n", data2);
  
  va_end(args1);
  va_end(args2);
}

static void __attribute__((noinline)) body() {
  void *baz = malloc(8);
  calls(1, baz);
}
MAIN(body)
