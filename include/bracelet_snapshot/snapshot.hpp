#pragma once
#include <cstdlib>
#include <stdio.h>
#include <unistd.h>

const char *BRACELET_SNAPSHOT_ENV_VAR = "BRACELET_SNAPSHOT";
void bracelet_snapshot() {
  if (std::getenv(BRACELET_SNAPSHOT_ENV_VAR)) {
    fclose(stderr);
    fclose(stdout);
    while (true) {
      sleep(1);
    }
  }
}