// TODO: these functions need to be updated with correct argument counts
#pragma clang diagnostic ignored "-Wincompatible-library-redeclaration"
#pragma clang diagnostic ignored "-Winvalid-noreturn"

#include <stddef.h>

void strtod(char *nptr, char **end) { *end = nptr; }
void strtof(char *nptr, char **end) { *end = nptr; }
void strtold(char *nptr, char **end) { *end = nptr; }
void strtol(char *nptr, char **end, int base) { *end = nptr; }
void strtoll(char *nptr, char **end, int base) { *end = nptr; }
void __isoc23_strtoll(char *nptr, char **end, int base) { *end = nptr; }
void __isoc23_strtoull(char *nptr, char **end, int base) { *end = nptr; }
char *strchr(char *s, int x) { return s; }
char *strrchr(char *s, int x) { return s; }
char *strchrnul(char *s, int x) { return s; }
char *stpcpy(char *dst, char *src) { return dst; }
char *strcpy(char *dst, char *src) { return dst; }
char *strncpy(char *dst, char *src, size_t n) { return dst; }
char *strcat(char *dst, char *src) { return dst; }
char *memcpy(char *dst, char *src) {
  *dst = *src;
  return dst;
}

void abort(void) {}
void accept(void) {}
void atoi(void) {}
void bind(void) {}
void close(void) {}
void toupper(void) {}
void utime(void) {}
void write(void) {}
void time(void) {}
void timerfd_create(void) {}
void timerfd_settime(void) {}
void sysconf(void) {}
void epoll_create(void) {}
void epoll_create1(void) {}
void epoll_ctl(void) {}
void epoll_wait(void) {}
void exit(void) {}
void explicit_bzero(void) {}
void free(void) {}
void fstat64(void) {}
void getentropy(void) {}
void getenv(void) {}
void getpeername(void) {}
void getpid(void) {}
void getrandom(void) {}
void getsockname(void) {}
void gettid(void) {}
void gettimeofday(void) {}
void gmtime_r(void) {}
void htonl(void) {}
void isspace(void) {}
void listen(void) {}
void localtime_r(void) {}
void fdatasync(void) {}
void fflush(void) {}
void log(void) {}
void log10(void) {}
void log2(void) {}
void open64(void) {}
void pause(void) {}
void pipe(void) {}
void poll(void) {}
void printf(void) {}
void nanosleep(void) {}
void munmap(void) {}
void munlock(void) {}
void mprotect(void) {}
void mlock(void) {}
void memcmp(void) {}
void memchr(void) {}
void madvise(void) {}
void raise(void) {}
void read(void) {}
void recv(void) {}
void sched_yield(void) {}
void sendmsg(void) {}
void setsockopt(void) {}
void shutdown(void) {}
void stat(void) {}
void strspn(void) {}
void strcmp(void) {}
void strncmp(void) {}
void streq(void) {}
void strneq(void) {}
void strlen(void) {}
void strftime(void) {}
void strerror(void) {}
void strerror_r(void) {}
void strcspn(void) {}
void socket(void) {}
void pow(void) {}
void frexp(void) {}
void bcmp(void) {}
void strdup(void) {}
void sprintf(void) {}
void perror(void) {}

int *__errno_location(void) {
  static int the_errno;
  return &the_errno;
}
