#include <dlfcn.h>
#include <stddef.h>

// The point of user2 is to generate another object file with the dlsym runtime
// embedded, to make sure that our weak references work properly.

void user2(void) {
  int Flag = 0;
  __asm__ __volatile__("" : "+r"(Flag));
  if (Flag) {
    dlsym(NULL, "some function, idk");
  }
}
