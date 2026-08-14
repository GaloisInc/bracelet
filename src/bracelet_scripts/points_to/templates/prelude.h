#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

extern unsigned long __nondeterministic_choice(void);
#define NONDET if (__nondeterministic_choice())
#define NONDET_SELECT(x, y) (__nondeterministic_choice() ? (x) : (y))

typedef void *T;

#define WEAK __attribute__((weak))
#define NO_INLINE __attribute__((noinline))
#define ALWAYS_INLINE __attribute__((always_inline))

#define OVERRIDE(x) bracelet_override_##x
#define DEF_OVERRIDE(x) WEAK NO_INLINE OVERRIDE(x)
// SVF treats some functions specially (see SVF's extapi.c). We want to call
// those functions directly, where we can.
#define DEF_OVERRIDE_INLINE(x) WEAK ALWAYS_INLINE OVERRIDE(x)

T DEF_OVERRIDE_INLINE(malloc)(T size) {
  (void)size;
  return malloc(__nondeterministic_choice());
}

T DEF_OVERRIDE_INLINE(realloc)(T old, T size) {
  (void)size;
  return realloc(old, __nondeterministic_choice());
}

T DEF_OVERRIDE_INLINE(reallocarray)(T old, T count, T size) {
  (void)count;
  (void)size;
  return reallocarray(old, __nondeterministic_choice(),
                      __nondeterministic_choice());
}

T DEF_OVERRIDE_INLINE(calloc)(T x, T y) {
  (void)x;
  (void)y;
  return calloc(__nondeterministic_choice(), __nondeterministic_choice());
}

T DEF_OVERRIDE_INLINE(posix_memalign)(T dst, T x, T y) {
  (void)x;
  (void)y;
  posix_memalign((void **)dst, __nondeterministic_choice(),
                 __nondeterministic_choice());
  return NULL;
}

T DEF_OVERRIDE(free)(T x) {
  (void)x;
  return NULL;
}

T DEF_OVERRIDE(strlen)(T x) {
  (void)x;
  return NULL;
}

T DEF_OVERRIDE(__strlen_evex)(T x) {
  (void)x;
  return NULL;
}

T DEF_OVERRIDE(strcmp)(T x, T y) {
  (void)x;
  (void)y;
  return NULL;
}

T DEF_OVERRIDE(strncmp)(T x, T y, T z) {
  (void)x;
  (void)y;
  (void)z;
  return NULL;
}

T DEF_OVERRIDE(bind)(T x, T y, T z) {
  (void)x;
  (void)y;
  (void)z;
  return NULL;
}

T DEF_OVERRIDE(accept)(T x, T y, T z) {
  (void)x;
  (void)y;
  (void)z;
  return NULL;
}

T DEF_OVERRIDE(accept4)(T w, T x, T y, T z) {
  (void)w;
  (void)x;
  (void)y;
  (void)z;
  return NULL;
}

T DEF_OVERRIDE(listen)(T x, T y) {
  (void)x;
  (void)y;
  return NULL;
}

T DEF_OVERRIDE(perror)(T x) {
  (void)x;
  return NULL;
}

T DEF_OVERRIDE(localeconv)(void) {
  static T contents;
  contents = malloc(__nondeterministic_choice());
  return contents;
}

T DEF_OVERRIDE(strstr)(T haystack, T needle) {
  (void)needle;
  return haystack;
}

T DEF_OVERRIDE(pthread_mutex_init)(T mutex, T mutexattr) {
  (void)mutex;
  (void)mutexattr;
  return NULL;
}
T DEF_OVERRIDE(pthread_mutex_lock)(T mutex) {
  (void)mutex;
  return NULL;
}
T DEF_OVERRIDE(pthread_mutex_trylock)(T mutex) {
  (void)mutex;
  return NULL;
}
T DEF_OVERRIDE(pthread_mutex_unlock)(T mutex) {
  (void)mutex;
  return NULL;
}
T DEF_OVERRIDE(pthread_mutex_destroy)(T mutex) {
  (void)mutex;
  return NULL;
}

T DEF_OVERRIDE(abort)(void) { return NULL; }

T DEF_OVERRIDE(atan2)(T x, T y) {
  (void)x;
  (void)y;
  return NULL;
}
T DEF_OVERRIDE(atan2f)(T x, T y) {
  (void)x;
  (void)y;
  return NULL;
}
T DEF_OVERRIDE(atan2l)(T x, T y) {
  (void)x;
  (void)y;
  return NULL;
}

T DEF_OVERRIDE(exp)(T x) {
  (void)x;
  return NULL;
}
T DEF_OVERRIDE(expf)(T x) {
  (void)x;
  return NULL;
}
T DEF_OVERRIDE(expl)(T x) {
  (void)x;
  return NULL;
}

T DEF_OVERRIDE(modf)(T x, T y) {
  (void)x;
  (void)y;
  return NULL;
}
T DEF_OVERRIDE(modff)(T x, T y) {
  (void)x;
  (void)y;
  return NULL;
}
T DEF_OVERRIDE(modfl)(T x, T y) {
  (void)x;
  (void)y;
  return NULL;
}

T DEF_OVERRIDE(eventfd)(T x, T y) {
  (void)x;
  (void)y;
  return NULL;
}

T DEF_OVERRIDE(bcmp)(T x, T y, T z) {
  (void)x;
  (void)y;
  (void)z;
  return NULL;
}

T DEF_OVERRIDE(clock_gettime)(T x, T y) {
  (void)x;
  (void)y;
  return NULL;
}

T DEF_OVERRIDE(close)(T x) {
  (void)x;
  return NULL;
}

T DEF_OVERRIDE(exit)(T x) {
  (void)x;
  return NULL;
}

T DEF_OVERRIDE_INLINE(mmap)(T addr, T len, T prot, T flags, T fd, T off) {
  (void)len;
  (void)prot;
  (void)flags;
  (void)fd;
  (void)off;
  return mmap(addr, __nondeterministic_choice(), __nondeterministic_choice(),
              __nondeterministic_choice(), __nondeterministic_choice(),
              __nondeterministic_choice());
}

T DEF_OVERRIDE(munmap)(T x, T y) {
  (void)x;
  (void)y;
  return NULL;
}

T DEF_OVERRIDE(memmem)(T haystack, T x, T y, T z) {
  (void)x;
  (void)y;
  (void)z;
  return haystack;
}

T DEF_OVERRIDE(qsort)(T base, T nmemb, T size, T cmp) {
  (void)nmemb;
  (void)size;
  ((T (*)(T, T))cmp)(base, base);
  return NULL;
}
T DEF_OVERRIDE(qsort_r)(T base, T nmemb, T size, T cmp, T ctx) {
  (void)nmemb;
  (void)size;
  ((T (*)(T, T, T))cmp)(base, base, ctx);
  return NULL;
}

T DEF_OVERRIDE(rand)(void) { return NULL; }
T DEF_OVERRIDE(rand_r)(T x) {
  (void)x;
  return NULL;
}
T DEF_OVERRIDE(srand)(T x) {
  (void)x;
  return NULL;
}
T DEF_OVERRIDE_INLINE(memcpy)(T x, T y, T sz) {
  (void)sz;
  return memcpy(x, y, __nondeterministic_choice());
}
T DEF_OVERRIDE_INLINE(memset)(T x, T c, T sz) {
  (void)c;
  (void)sz;
  return memset(x, __nondeterministic_choice(), __nondeterministic_choice());
}
T DEF_OVERRIDE(memchr)(T str, T c, T n) {
  (void)c;
  (void)n;
  return str;
}
T DEF_OVERRIDE(memrchr)(T str, T c, T n) {
  (void)c;
  (void)n;
  return str;
}

typedef struct BraceletFile {
  T cookie;
  cookie_io_functions_t funcs;
} BraceletFile;

// Inline because it allocates.
T DEF_OVERRIDE_INLINE(fopen)(T name, T mode) {
  (void)name;
  (void)mode;
  return calloc(1, sizeof(BraceletFile));
}
// Inline because it allocates.
T DEF_OVERRIDE_INLINE(fdopen)(T fd, T mode) {
  (void)fd;
  return calloc(1, sizeof(BraceletFile));
}

// TODO: test this!
// Inline because it allocates.
T DEF_OVERRIDE_INLINE(fopencookie)(T cookie, T mode, T funcs) {
  BraceletFile *out = (BraceletFile *)calloc(1, sizeof(BraceletFile));
  (void)mode;
  out->cookie = cookie;
  // Even though the cookie functions struct is passed by value, LLVM compiles
  // it into a pointer.
  out->funcs = *(cookie_io_functions_t *)funcs;
  return (void *)out;
}

T DEF_OVERRIDE(clearerr)(T x) {
  (void)x;
  return NULL;
}
T DEF_OVERRIDE(feof)(T x) {
  (void)x;
  return NULL;
}
T DEF_OVERRIDE(ferror)(T x) {
  (void)x;
  return NULL;
}
T DEF_OVERRIDE(fileno)(T x) {
  (void)x;
  return NULL;
}

T DEF_OVERRIDE(pthread_create)(T thread_out, T attr, T start_routine, T arg) {
  // We need to indicate that arg is passed to start_routine.
  // Technically, this doesn't happen due to it being called from
  // pthread_create() but we model it like this for SVF.
  (void)thread_out;
  (void)attr;
  ((T (*)(T))start_routine)(arg);
  return NULL;
}

T DEF_OVERRIDE_INLINE(_Znwm)(T sz) {
  (void)sz;
  return malloc(__nondeterministic_choice());
}
