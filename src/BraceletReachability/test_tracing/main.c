#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

static void braceletTraceWriteAll(int Fd, uint8_t *Buf, size_t Len) {
  while (Len > 0) {
    ssize_t Rc = write(Fd, Buf, Len);
    if (Rc < 0) {
      if (errno != EINTR) {
        perror("write()");
        abort();
      }
    }
    Len -= Rc;
    Buf += Rc;
  }
}
static void listFds(void) {
  struct dirent *Dentry;
  DIR *Dir = opendir("/proc/self/fd");
  if (Dir == NULL) {
    perror("opendir(/proc/self/fd)");
    abort();
  }
  printf("==== /proc/self/fd\n");
  char Buf[1024];
  while ((Dentry = readdir(Dir))) {
    if (strcmp(Dentry->d_name, ".") == 0 || strcmp(Dentry->d_name, "..") == 0)
      continue;
    if (atoi(Dentry->d_name) == dirfd(Dir))
      continue;
    printf("%s", Dentry->d_name);
    ssize_t Rc = readlinkat(dirfd(Dir), Dentry->d_name, Buf, sizeof(Buf) - 1);
    if (Rc >= 1) {
      Buf[Rc] = '\0';
      printf(" -> %s", Buf);
    }
    printf("\n");
  }
  printf("---- /proc/self/fd\n");
  closedir(Dir);
}
static void listMaps(void) {
  printf("==== /proc/self/maps\n");
  fflush(stdout);
  int Fd = open("/proc/self/maps", O_RDONLY);
  if (Fd < 0) {
    perror("open(/proc/self/maps)");
    abort();
  }
  uint8_t Buf[4096];
  while (1) {
    ssize_t Rc = read(Fd, Buf, sizeof(Buf));
    if (Rc == 0) {
      break;
    }
    if (Rc < 0) {
      perror("read(/proc/self/maps)");
      abort();
    }
    braceletTraceWriteAll(STDOUT_FILENO, Buf, Rc);
  }
  close(Fd);
  printf("---- /proc/self/maps\n");
}

void testCode(void);       // Defined by python
void printFunctions(void); // Defined by python

static void *backgroundThread(void *Ignored) {
  testCode();
  testMegamorphic();
  return NULL;
}

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

int main(int argc, const char *argv[]) {
  printFunctions();
  printf("==== pid\n%lu\n---- pid\n", (unsigned long)getpid());
  printf("==== testMegamorphic\n%p\n---- testMegamorphic\n",
         (void *)testMegamorphic);
  assert(NUM_THREADS >= 1);
  pthread_t Threads[NUM_THREADS];
  for (int I = 0; I < NUM_THREADS; I++) {
    if (pthread_create(&Threads[I], NULL, backgroundThread, NULL) != 0) {
      perror("pthread_create()");
      abort();
    }
  }
  for (int I = 0; I < NUM_THREADS; I++) {
    pthread_join(Threads[I], NULL);
  }
  listFds();
  listMaps();
  return coredump_self();
}
