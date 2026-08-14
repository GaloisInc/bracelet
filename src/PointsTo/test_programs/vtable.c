// Test argument-less function (pointers)

#include "common.h"

bool which[] = {true, false, true, false};

struct Vtable {
  void (*f)(struct Vtable *);
};

static void foo(struct Vtable *v) { printf("I am foo! %p\n", (void *)v); }
static void bar(struct Vtable *v) { printf("I am bar! %p\n", (void *)v); }

static struct Vtable FOO = {foo};
static struct Vtable BAR = {bar};

static void __attribute__((noinline)) body() {
  for (size_t i = 0; i < sizeof(which); i++) {
    struct Vtable *v = &FOO;
    if (which[i]) {
      v = &BAR;
    }
    v->f(v);
  }
}
MAIN(body)
