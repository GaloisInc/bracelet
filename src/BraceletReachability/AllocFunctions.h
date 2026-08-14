#pragma once

// Functions that allocate
#define BRACELET_ALLOC_FUNCTION_NAMES                                            \
  {"aligned_alloc",                                                            \
   "realloc",                                                                  \
   "_Znam" /*operatornew[](unsigned long)*/,                                   \
   "__libc_malloc",                                                            \
   "_ZnamRKSt9nothrow_t", /*operator new[](unsigned long, std::nothrow_t       \
                             const&)*/                                         \
   "_ZnwmRKSt9nothrow_t", /*operator new(unsigned long, std::nothrow_t         \
                             const&)*/                                         \
   "__libc_memalign",                                                          \
   "memalign",                                                                 \
   "__libc_realloc",                                                           \
   "reallocarray",                                                             \
   "__libc_reallocarray",						       \
   "calloc",                                                                   \
   "_Znwm", /*operator new(unsigned long)*/                                    \
   "malloc",                                                                   \
   "__libc_calloc"}
