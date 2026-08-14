#include "common.h"

struct Vtable {
  void (*f)(struct Vtable *);
};

static void foo(struct Vtable *v) { printf("I am foo! %p\n", (void *)v); }
static void bar(struct Vtable *v) { printf("I am bar! %p\n", (void *)v); }

static void __attribute__((noinline)) set_vtable_foo(struct Vtable *dst) {
  dst->f = foo;
}
static void __attribute__((noinline)) set_vtable_bar(struct Vtable *dst) {
  dst->f = bar;
}

static void __attribute__((noinline)) body() {
  struct Vtable vFoo, vBar;
  set_vtable_foo(&vFoo);
  set_vtable_bar(&vBar);
  vFoo.f(&vFoo);
  vBar.f(&vBar);
}
MAIN(body)
