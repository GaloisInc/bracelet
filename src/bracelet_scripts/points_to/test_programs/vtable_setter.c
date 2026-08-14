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

static void __attribute__((noinline)) set_vtable_foo(struct Vtable **dst) {
  *dst = &FOO;
}
static void __attribute__((noinline)) set_vtable_bar(struct Vtable **dst) {
  *dst = &BAR;
}

static void __attribute__((noinline)) body() {
  for (size_t i = 0; i < sizeof(which); i++) {
    struct Vtable *v;
    set_vtable_foo(&v);
    if (which[i]) {
      set_vtable_bar(&v);
    }
    v->f(v);
  }
}
MAIN(body)
