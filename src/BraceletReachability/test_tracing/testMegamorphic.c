#include "common.h"
#include <stddef.h>

void __attribute__((noinline)) testMegamorphic(void) {
  for (int I = 0; I < NUM_ITERATIONS; I++) {
    for (func_t *G = GFunctions; *G != NULL; G++) {
      (blackBox(*G))();
    }
  }
}
