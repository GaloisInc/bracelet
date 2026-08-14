#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, const char *argv[]) {
  uid_t Euid = geteuid();
  if (Euid != 0) {
    fprintf(stderr, "Expected euid to be root. Got %d\n", (int)Euid);
    return 1;
  }
  if (setuid(0) != 0) {
    perror("CHILD: setuid()");
    return 1;
  }
  const char *Snapshot = "/opt/bracelet-llvm/bin/snapshot-launcher.sh";
  execlp(Snapshot, Snapshot, NULL);
  perror("CHILD: exec()");
  return 1;
}
