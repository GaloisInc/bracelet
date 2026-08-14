#include <assert.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// #define NUM_THREADS 128
#ifndef NUM_THREADS
#define NUM_THREADS 1
#endif

#ifndef NUM_FUNCTIONS
#define NUM_FUNCTIONS (1 << 15)
#endif

struct ThreadState {
  void *Lib;
  atomic_int Started;
};

static atomic_int ThreadId = 0;

static void *background(void *StateRaw) {
  struct ThreadState *State = (struct ThreadState *)StateRaw;
  while (!atomic_load_explicit(&State->Started, memory_order_relaxed)) {
  }
  void *Lib = State->Lib;
  char Buf[1024] = {0};
  int OurThreadId =
      atomic_fetch_add_explicit(&ThreadId, 1, memory_order_relaxed);
  int PerThread = NUM_FUNCTIONS / NUM_THREADS;
  static_assert(NUM_FUNCTIONS % NUM_THREADS == 0,
                "NUM_FUNCTIONS must be a multiple of NUM_THREADS");
  _Pragma("clang loop unroll(disable) vectorize(disable)") for (int I = 0;
                                                                I < PerThread;
                                                                I++) {
    snprintf(Buf, sizeof(Buf), "f_%d", OurThreadId * PerThread + I);
    void *Sym = dlsym(Lib, Buf);
    if (Sym == NULL) {
      fprintf(stderr, "dlsym(%s): %s\n", Buf, dlerror());
      abort();
    }
  }
  return NULL;
}

extern void user2(void);

int main() {
  printf("Running dlsym test!\n");
  user2();
  // we leak the result of realpath
  void *Lib = dlopen(realpath("./lib.so", NULL), RTLD_NOW);
  if (Lib == NULL) {
    fprintf(stderr, "dlopen(): %s\n", dlerror());
    abort();
  }
  struct ThreadState State = {Lib, 0};
  pthread_t Threads[NUM_THREADS];
  for (int I = 0; I < NUM_THREADS; I++) {
    pthread_create(&Threads[I], NULL, background, &State);
  }
  atomic_store_explicit(&State.Started, 1, memory_order_relaxed);
  for (int I = 0; I < NUM_THREADS; I++) {
    pthread_join(Threads[I], NULL);
  }
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
