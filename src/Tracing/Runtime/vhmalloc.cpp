// From
// https://gitlab-ext.galois.com/radss/vhmalloc/-/blob/d63edbb16de64d7a9bcb7dfb02b566b12b150e58/vhmalloc.c

#include "common.h"

#include <array>
#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <limits>
#include <malloc.h>
#include <new>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "heaps/utility/sysmallocheap.h"
// The example includes this cpp file, so we do the same.
#include "wrappers/generic-memalign.cpp" // NOLINT(bugprone-suspicious-include)

#include "vhmalloc.h"

using namespace vhmalloc;

namespace {
thread_local unsigned int seed = 42;
size_t overprovision = 3;

typedef struct pool pool_t;
typedef struct slab slab_t;

struct pool {
  size_t element_size;
  size_t free;
  slab_t *free_head;
  slab_t *free_tail;
  slab_t *full_head;
  pthread_mutex_t lock;
};

struct slab {
  pool_t *pool;
  size_t capacity;
  size_t free;
  uint8_t *objects;
  slab_t *prev;
  slab_t *next;
  slab_t *free_prev;
  slab_t *free_next;
  uint64_t ids[];
};

struct alignas(HL::SysMallocHeap) {
  uint8_t storage[sizeof(HL::SysMallocHeap)];
  bool initialized;

  HL::SysMallocHeap &operator*() {
    // This isn't thread-safe, but init should happen while constructors run, so
    // it shouldn't be an issue.
    if (!initialized) {
      new (storage) HL::SysMallocHeap();
      initialized = true;
    }
    return *reinterpret_cast<HL::SysMallocHeap *>(storage);
  }
  HL::SysMallocHeap *operator->() { return &operator*(); }
} sys_malloc = {{0}, false};

/* Slab space */

#define LEVEL2MASK (((uintptr_t)0x1 << 30) - 1)

// NULL before it's initialized.
std::array<slab_t **, 1 << 17> slabMap = {nullptr};

slab_t *getSlab(uintptr_t ptr) {
  // Technically we could just return nullptr if slabMap is null, but that'd
  // probably be indicative of an initialization bug.
  // Level 1 lookup
  size_t offset = ptr >> 30;
  if (__builtin_expect(offset >= slabMap.size(), false))
    return nullptr;
  slab_t **region = slabMap[offset];
  if (region == NULL) {
    return NULL;
  }

  // Level 2 lookup
  size_t page = (ptr & LEVEL2MASK) >> 12;
  return region[page];
}

void markSlab(void *ptr, slab_t *slab) {
  // Level 1 lookup
  size_t offset = (uintptr_t)ptr >> 30;
  slab_t **region = slabMap[offset];
  if (region == NULL) {
    // Allocate a map for the region
    region = reinterpret_cast<slab_t **>(
        mmap(NULL, (0x1 << 18) * sizeof(slab_t *), PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(reinterpret_cast<void *>(region) != MAP_FAILED);
    slabMap[offset] = region;
  }

  // Level 2 lookup
  size_t page = ((uintptr_t)ptr & LEVEL2MASK) >> 12;
  region[page] = slab;
}

void allocateSlab(pool_t *pool, size_t count) {
  size_t toAllocate =
      count == 0 ? pool->element_size : (pool->element_size * count);
  size_t numPages = (toAllocate % 4096 == 0) ? (toAllocate / 4096)
                                             : ((toAllocate / 4096) + 1);
  size_t capacity = (numPages * 4096) / pool->element_size;

  slab_t *slab = (slab_t *)sys_malloc->malloc(sizeof(slab_t) +
                                              capacity * sizeof(uint64_t));
  slab->pool = pool;
  slab->capacity = capacity;
  slab->free = capacity;
  pool->free += capacity;
  memset(&slab->ids[0], 0, capacity * sizeof(slab->ids[0]));

  slab->objects = reinterpret_cast<uint8_t *>(
      mmap(NULL, numPages * 4096, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));

  for (size_t i = 0; i < numPages; i++) {
    markSlab(&slab->objects[i * 4096], slab);
  }

  /* Add to the full list */
  slab->prev = NULL;
  slab->next = pool->full_head;
  if (slab->next != NULL) {
    slab->next->prev = slab;
  }
  pool->full_head = slab;

  /* Add to the free list */
  /* We know it goes at the start! */
  slab->free_prev = NULL;
  slab->free_next = pool->free_head;
  if (slab->free_next != NULL) {
    slab->free_next->free_prev = slab;
  } else {
    pool->free_tail = slab;
  }
  pool->free_head = slab;
}

// If object_offset is a non-NULL pointer, it will be populated with the
// pointer's offset into the object.
size_t getElementIndex(pool_t *pool, slab_t *slab, uintptr_t ptr,
                       size_t *object_offset = nullptr) {
  off_t slab_offset = ((intptr_t)ptr) - ((intptr_t)slab->objects);
  size_t index = slab_offset / pool->element_size;
  size_t object_offset_value = slab_offset % pool->element_size;
  if (object_offset)
    *object_offset = object_offset_value;
  assert(slab_offset >= 0 && index < slab->capacity &&
         "pointer outside of slab");
  return index;
}

void *getElementPtr(pool_t *pool, slab_t *slab, size_t index) {
  assert(index < slab->capacity && "index outside of slab");
  return (void *)(&slab->objects[index * pool->element_size]);
}

void makeFree(pool_t *pool, slab_t *slab) {
  for (slab_t *cur = slab->prev; cur != NULL; cur = cur->prev) {
    // Is a predecessor on the free list?
    if (cur->free > 0) {
      // Then insert this node after it
      slab->free_next = cur->free_next;
      slab->free_prev = cur;
      cur->free_next = slab;
      if (slab->free_next != NULL) {
        slab->free_next->free_prev = slab;
      } else {
        pool->free_tail = slab;
      }
      return;
    }
  }
  // No predecessors were on the free list, stick it at the front
  slab->free_prev = NULL;
  slab->free_next = pool->free_head;
  if (slab->free_next != NULL) {
    slab->free_next->free_prev = slab;
  } else {
    pool->free_tail = slab;
  }
  pool->free_head = slab;
}

void makeFull(pool_t *pool, slab_t *slab) {
  // Remove from the free list
  if (slab->free_prev != NULL) {
    slab->free_prev->free_next = slab->free_next;
  } else {
    pool->free_head = slab->free_next;
  }
  if (slab->free_next != NULL) {
    slab->free_next->free_prev = slab->free_prev;
  }
}

void poolinit(pool_t *pool, size_t element_size) {
  pool->free = 0;
  pool->free_head = NULL;
  pool->free_tail = NULL;
  pool->full_head = NULL;
  pool->element_size = element_size;
  pthread_mutex_init(&pool->lock, NULL);
}

void *allocate(pool_t *pool, Tag tag) {
  assert(tag != 0);
  pthread_mutex_lock(&pool->lock);
  if (pool->free <= overprovision) {
    allocateSlab(pool, 2 * overprovision);
  }

  int rand = rand_r(&seed);
  size_t offset = rand % pool->free;

  slab_t *slab = pool->free_head;
  while (offset >= slab->free) {
    offset -= slab->free;
    slab = slab->free_next;
  }

  for (size_t i = 0; i < slab->capacity; i++) {
    if (slab->ids[i] == 0) {
      if (offset == 0) {
        slab->ids[i] = tag;
        slab->free--;
        pool->free--;
        if (slab->free == 0) {
          makeFull(pool, slab);
        }
        pthread_mutex_unlock(&pool->lock);
        void *alloc = getElementPtr(pool, slab, i);
        memset(alloc, 0, pool->element_size);
        return alloc;
      } else {
        offset--;
      }
    }
  }
  assert(false && "free counts said there was enough space");
}

void deallocate(slab_t *slab, void *ptr) {
  pthread_mutex_lock(&slab->pool->lock);
  size_t index = getElementIndex(slab->pool, slab, (uintptr_t)ptr);
  slab->ids[index] = 0;
  slab->free++;
  slab->pool->free++;
  if (slab->free == 1) {
    makeFree(slab->pool, slab);
  }
  pthread_mutex_unlock(&slab->pool->lock);
}

PointerInfo getid(slab_t *slab, uintptr_t ptr) {
  size_t object_offset;
  size_t index = getElementIndex(slab->pool, slab, ptr, &object_offset);
  uint64_t raw_id = slab->ids[index];
  if (raw_id == 0) {
    // The slot is empty
    return {0, 0};
  }
  return {raw_id, object_offset};
}

/* Wrapper interface */

#define NUMPOOLS 26

enum class MallocThreadState { Uninit = 0, Ready, InsideMalloc };
thread_local auto thread_state = MallocThreadState::Uninit;
thread_local pool_t sizedPools[NUMPOOLS];
} // namespace

extern "C" void *xxmalloc(size_t size) {
  if (size == 0) {
    return NULL;
  }

  if (__builtin_expect(thread_state != MallocThreadState::Ready, false)) {
    if (thread_state == MallocThreadState::InsideMalloc) {
      // This memory will end up getting leaked.
      return sys_malloc->malloc(size);
    }
    thread_state = MallocThreadState::InsideMalloc;
    for (int i = 0; i < NUMPOOLS; i++) {
      poolinit(&sizedPools[i], 1UL << (i + 4));
    }
    thread_state = MallocThreadState::Ready;
  }
  thread_state = MallocThreadState::InsideMalloc;

  size_t logSize = (1 + (63 - __builtin_clzl(size - 1)));
  size_t bin = (logSize < 4) ? 0 : logSize - 4;
  assert(bin < NUMPOOLS && "allocation too big");

  void *ptr = allocate(&sizedPools[bin], std::numeric_limits<Tag>::max());
  thread_state = MallocThreadState::Ready;
  return ptr;
}

extern "C" void xxfree(void *ptr) {
  if (ptr == NULL) {
    return;
  }
  slab_t *slab = getSlab((uintptr_t)ptr);
  if (slab != NULL) {
    deallocate(slab, ptr);
  }
}

extern "C" size_t xxmalloc_usable_size(void *ptr) {
  slab_t *slab = getSlab((uintptr_t)ptr);
  if (slab != NULL) {
    return slab->pool->element_size;
  } else {
    return 0;
  }
}

extern "C" void xxmalloc_lock() { return; }

extern "C" void xxmalloc_unlock() { return; }

extern "C" void *xxmemalign(size_t alignment, size_t size) {
  return generic_xxmemalign(alignment, size);
}

EXPORT PointerInfo PointerInfo::of(uintptr_t ptr) {
  slab_t *slab = getSlab(ptr);
  if (slab == nullptr) {
    return {0, 0};
  }
  return getid(slab, ptr);
}

EXPORT void vhmalloc::setTag(void *ptr_ptr, Tag tag) {
  assert(tag != 0);
  uintptr_t ptr = reinterpret_cast<uintptr_t>(ptr_ptr);
  slab_t *slab = getSlab(ptr);
  assert(slab != nullptr);
  size_t index = getElementIndex(slab->pool, slab, ptr);
  auto *tag_ptr = &slab->ids[index];
  assert(*tag_ptr == std::numeric_limits<Tag>::max());
  *tag_ptr = tag;
}
