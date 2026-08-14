#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int foo(int x, const char *) { return x + 1; }

void *__attribute__((section("my_ptr"))) the_ptr = (void *)foo;

int main() {
  char cmd_buffer[1024];
  snprintf(cmd_buffer, sizeof(cmd_buffer), "gcore %d", (int)getpid());
  return system(cmd_buffer);
}
