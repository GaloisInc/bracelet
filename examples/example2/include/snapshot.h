#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

static inline void bracelet_snapshot(void) {
  if (getenv("BRACELET_SNAPSHOT")) {
    fclose(stderr);
    fclose(stdout);
    while (1) {
      sleep(1);
    }
  }
}
