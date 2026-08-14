#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// An uninitialized global pointer. It lands in .bss, a NOBITS (zero-filled)
// section, so the linker emits no relocation for its storage: at load time it
// is simply NULL. This mirrors the dlsym page-list head pointer that
// bracelet-edges follows.
void *bss_pointer;

// A pointer to the .bss slot above, placed in a named section so the test can
// recover the (section, offset) address of bss_pointer via resolvePointers.
// This slot *is* relocated (R_X86_64_RELATIVE), so resolving it yields the
// address of bss_pointer itself.
void **__attribute__((section("bss_pointer_holder"))) holder = &bss_pointer;

int main() {
  char cmd_buffer[1024];
  snprintf(cmd_buffer, sizeof(cmd_buffer), "gcore %d", (int)getpid());
  return system(cmd_buffer);
}
