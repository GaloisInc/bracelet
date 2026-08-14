#include "common.h"
#include <string.h>

static void __attribute__((noinline)) print_it(const char *arg) { puts(arg); }
static void __attribute__((noinline)) body() {
  char msg[] = "this is a test";
  print_it(msg);
  // char *msg2 = malloc(sizeof(msg));
  // memcpy(msg2, msg, sizeof(msg));
  // print_it(msg2);
}
MAIN(body)
