// Test argument-less function (pointers)

#include "common.h"

uint8_t which[] = {0, 1, 2, 0, 1, 2};

struct Vtable {
  void (*f)(struct Vtable *);
};

static void foo(struct Vtable *v) { printf("I am foo! %p\n", (void *)v); }
static void bar(struct Vtable *v) { printf("I am bar! %p\n", (void *)v); }
static void baz(struct Vtable *v) { printf("I am baz! %p\n", (void *)v); }

static struct Vtable FOO = {foo};
static struct Vtable BAR = {bar};
static struct Vtable BAZ = {baz};
static struct Vtable *VTABLES[] = {&FOO, &BAR, &BAZ};

static void __attribute__((noinline)) body() {
  for (size_t i = 0; i < sizeof(which); i++) {
    struct Vtable *v = VTABLES[which[i]];
    v->f(v);
  }
}
MAIN(body)
