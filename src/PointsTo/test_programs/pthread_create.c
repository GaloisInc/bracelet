
#include "common.h"

#include <pthread.h>

bool which[] = {true, false, true, false};

static void foo(int x) { printf("I am foo %d!\n", x); }
static void bar(int x) { printf("I am bar %d!\n", x); }

static void *background(void *func) {
  ((void (*)(int))func)(12);
  return NULL;
}

static void __attribute__((noinline)) body() {
  for (size_t i = 0; i < sizeof(which); i++) {
    pthread_t t;
    pthread_create(&t, NULL, background, (void *)(which[i] ? foo : bar));
    pthread_join(t, NULL);
  }
}
MAIN(body)
