#include "common.h"

#include <assert.h>
#include <dlfcn.h>
#include <string.h>

static void __attribute__((noinline)) body() {
  char *buf = malloc(8);
  strcpy(buf, "hello world\n");

  void* handle = dlopen("./test.so", RTLD_LAZY);
  void (*notPresent)(void) = (void (*)(void))dlsym(handle, "does_not_exist");
  assert(notPresent == NULL);
  void *(*transferFn)(void *) = (void *(*)(void *))dlsym(handle, "transfer");
  char *msg = transferFn(buf);
  printf("%s", msg);
}

MAIN(body)
