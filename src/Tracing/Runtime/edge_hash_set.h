#pragma once

#define _GNU_SOURCE 1

#include <assert.h>
#include <boost/hash2/fnv1a.hpp>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "BraceletRuntimeStructs_c.h"

namespace bracelet_trace {
// A hash set which persists its contents to disk.
struct EdgeHashSet {
  // Persist trace data inside base_path
  EdgeHashSet(const char *base_path, unsigned log2_initial_buffer_size) {
    assert(!is_ready());
    epoch = 0;
    edge_count = 1 << log2_initial_buffer_size;
    assert(base_path && "Set BRACELET_TRACE_DIR env var");
    char dst_path[1024];
    snprintf(dst_path, sizeof(dst_path), "%s/trace-%lu_XXXXXX", base_path,
             (unsigned long)getpid());
    fd = mkostemp(dst_path, O_CLOEXEC);
    if (fd < 0) {
      perror("mkostemp()");
      abort();
    }
    if (ftruncate(fd, edge_size()) < 0) {
      perror("ftruncate()");
      abort();
    }
    edges = reinterpret_cast<BraceletTraceEdge *>(
        mmap(NULL, edge_size(), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (edges == MAP_FAILED) {
      perror("mmap()");
      abort();
    }
    update_resize_threshold();
    assert(is_ready());
  }
  EdgeHashSet(const EdgeHashSet &) = delete;
  EdgeHashSet(EdgeHashSet &&) = delete;
  EdgeHashSet &operator=(const EdgeHashSet &) = delete;
  EdgeHashSet &operator=(EdgeHashSet &&) = delete;
  ~EdgeHashSet() {
    assert(is_ready());
    close(fd);
    munmap(reinterpret_cast<void *>(edges), edge_size());
    edges = nullptr;
    assert(!is_ready());
  }
  // Return true if an addition acctually occurred.
  bool add_edge(BraceletTraceEdge e) {
    if (__builtin_expect(!is_ready(), false))
      return false;
    assert(edge_epoch(e) == 0);
    if (__builtin_expect(inserted_edges >= resize_threshold, false)) {
      increase_size();
      assert(inserted_edges < resize_threshold);
    }
    // Do this _after_ any potential resize so we set the epoch on the new edge.
    e = edge_set_epoch(e, epoch);
    IndexSequence seq(*this, e);
    while (true) {
      auto idx = seq();
      auto &dst = edges[idx];
      if (!edge_present(dst)) {
        inserted_edges++;
        dst = e;
        return true;
      }
      if (edge_equal(dst, e))
        return false;
    }
  }

protected: // Not private to these can be used in tests
  // The index sequence determines where we should look next for a space to
  // insert an edge.
  struct IndexSequence {
    IndexSequence(const EdgeHashSet &hs, const BraceletTraceEdge &e)
        : edge_count(hs.edge_count), hash(edge_hash(e)) {}
    size_t operator()() {
      if (attempts_remaining == 0)
        assert(0);
      if (attempts_remaining % 4 == 3) {
        // Murmur64
        hash ^= hash >> 33;
        hash *= 0xff51afd7ed558ccdULL;
        hash ^= hash >> 33;
        hash *= 0xc4ceb9fe1a85ec53ULL;
        hash ^= hash >> 33;
      } else {
        hash++;
      }
      attempts_remaining--;
      return hash & (edge_count - 1);
    }

  private:
    size_t edge_count;
    uint64_t hash;
    size_t attempts_remaining = 1024;
  };

  static uint64_t edge_hash(BraceletTraceEdge e) {
    // Zero the LSB of "to," since we use it for hash table housekeeping.
    e.trace_site = (e.trace_site >> 1) << 1;
    boost::hash2::fnv1a_64 h;
    h.update(&e, sizeof(BraceletTraceEdge));
    return h.result();
  }
  static bool edge_present(const BraceletTraceEdge &e) {
    return e.trace_site != 0;
  }
  static uint8_t edge_epoch(const BraceletTraceEdge &e) {
    return e.trace_site & 1;
  }
  static bool edge_equal(const BraceletTraceEdge &a, const BraceletTraceEdge &b) {
    return (a.trace_site >> 1) == (b.trace_site >> 1) && a.value == b.value;
  }
  static BraceletTraceEdge edge_set_epoch(const BraceletTraceEdge &e,
                                        uint8_t epoch) {
    return {((e.trace_site >> 1) << 1) | static_cast<uint64_t>(epoch), e.value};
  }
  static constexpr BraceletTraceEdge EDGE_ZERO = {0, 0};
  // Try to insert edge into its new hash table position.
  // If this function returns true, then this function should be called again on
  // the same slot.
  bool resize_insert(BraceletTraceEdge &edge) {
    // If edge_epoch(edge) == epoch, that means this edge has been relocated
    // as part of this resizing.
    if (!edge_present(edge) || edge_epoch(edge) == epoch)
      return false;
    edge = edge_set_epoch(edge, epoch);
    IndexSequence seq(*this, edge);
    while (true) {
      auto &dst = edges[seq()];
      if (edge_present(dst)) {
        if (edge_epoch(dst) == epoch) {
          // This edge conflicts with an edge that we've already re-hashed.
          if (edge_equal(edge, dst))
            return false;
        } else {
          // This edge conflicts with an edge we haven't re-hashed.
          // Put our edge in its destination, evicting the old edge. We put
          // the old edge into the edge variable for use to re-hash.
          std::swap(edge, dst);
          // TODO: if the process exits right here, we'll have lost edge.
          return true;
        }
      } else {
        dst = edge_set_epoch(edge, epoch);
        edge = EDGE_ZERO;
        return false;
      }
    }
  }
  void increase_size() {
    epoch ^= 1;
    auto old_count = edge_count;
    auto old_size = edge_size();
    edge_count *= 2;
    if (ftruncate(fd, edge_size()) < 0) {
      perror("ftruncate()");
      abort();
    }
    edges = reinterpret_cast<BraceletTraceEdge *>(
        mremap(reinterpret_cast<void *>(edges), old_size, edge_size(),
               MREMAP_MAYMOVE));
    // Rehash everything. See the note on epoch for how this happens.
    for (size_t i = 0; i < old_count; i++) {
      if (__builtin_expect(resize_insert(edges[i]), false)) {
        while (resize_insert(edges[i])) {
        }
      }
    }
    update_resize_threshold();
  }
  // Other thread local consturctors and destructors might run alongside this
  // code. We manually track the state here to make sure that nothing breaks if
  // constructors/destructors run in a bad order.
  bool is_ready() const { return edges != nullptr; }
  size_t edge_size() { return edge_count * sizeof(BraceletTraceEdge); }
  void update_resize_threshold() { resize_threshold = (edge_count * 3) / 4; }

  // Should be a power of 2
  size_t edge_count = 0;
  size_t inserted_edges = 0;
  size_t resize_threshold = 0;
  BraceletTraceEdge *edges = nullptr;
  int fd = 0;
  // Which epoch (edge_count) was this edge hashed under.
  // To avoid needing to allocate an additional buffer to copy our data into
  // when resizing, we perform a rehash in-place when we resize the buffer. To
  // facilitate this, we need to know whether we've already re-hashed a value or
  // not. We use one bit of the trace edge to denote the current epoch that that
  // edge was hashed under. If the epochs differ, then we need to rehash the
  // item.
  uint8_t epoch = 0;
};

} // namespace bracelet_trace
