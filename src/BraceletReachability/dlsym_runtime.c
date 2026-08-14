// Record in-memory the result of dlsym() invocations. This data will be
// recovered from the coredump later.

#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "BraceletRuntimeStructs_c.h"

void __attribute__((weak))
braceletReachabilityDlsymPageInsert(void *Symbol,
                                  _Atomic(struct DlsymPage *) *Dst) {
  struct DlsymPage *Page = atomic_load_explicit(Dst, memory_order_acquire);
  while (1) {
    if (Page != NULL) {
      uint64_t Idx =
          atomic_fetch_add_explicit(&Page->count, 1, memory_order_relaxed);
      if (Idx < (sizeof(Page->pointers) / sizeof(void *))) {
        atomic_store_explicit(&Page->pointers[Idx], Symbol,
                              memory_order_relaxed);
        return;
      }
    }
    struct DlsymPage *Allocated =
        mmap(NULL, sizeof(struct DlsymPage), PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (Allocated == MAP_FAILED) {
      perror("Bracelet dlsym mmap() error");
      abort();
    }
    memset(Allocated, 0, sizeof(struct DlsymPage));
    Allocated->next = Page;
    struct DlsymPage *Expected = Page;
    if (atomic_compare_exchange_strong_explicit(Dst, &Expected, Allocated,
                                                memory_order_release,
                                                memory_order_acquire)) {
      Page = Allocated;
    } else {
      // We won't worry about munmap() failing.
      munmap(Allocated, sizeof(struct DlsymPage));
      Page = Expected;
    }
  }
}
