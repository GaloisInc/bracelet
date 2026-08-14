#pragma once
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int coredump_self() {
  FILE *F = fopen("/proc/self/coredump_filter", "w");
  if (F == NULL) {
    perror("fopen(coredump_filter)");
    abort();
  }
  if (fprintf(F, "0xfff") != 5) {
    perror("fprintf(coredump_filter)");
    abort();
  }
  if (fclose(F) != 0) {
    perror("fclose(coredump_filter)");
    abort();
  }
  int Pid = getpid();
  char CmdBuffer[1024] = {0};
  snprintf(CmdBuffer, sizeof(CmdBuffer), "gcore %d", Pid);
  return system(CmdBuffer);
}

#define MAIN(inner)                                                            \
  int main() {                                                                 \
    inner();                                                                   \
    return coredump_self();                                                    \
  }
