#pragma once

#ifndef NUM_THREADS
#define NUM_THREADS 1
#endif

#ifndef NUM_ITERATIONS
#define NUM_ITERATIONS 1
#endif

typedef void (*func_t)(void);
static func_t blackBox(func_t X) {
  __asm__ __volatile__("" : "+r"(X));
  return X;
}
extern func_t GFunctions[]; // Defined by python

void testMegamorphic(void);
