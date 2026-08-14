#include "absl/container/flat_hash_map.h"
#include <iostream>
#include <map>
#include <random>
#include <tuple>

#include "BraceletRuntimeStructs_c.h"
#include "edge_hash_set.h"

namespace {
struct CanonicalEdge : public std::tuple<uint64_t, uint64_t> {
  CanonicalEdge(BraceletTraceEdge e)
      : std::tuple<uint64_t, uint64_t>((e.trace_site >> 1) << 1, e.value) {}
};
struct TestingEdgeHashSet : public bracelet_trace::EdgeHashSet {
  TestingEdgeHashSet(const char *base_path, unsigned log2_initial_buffer_size)
      : bracelet_trace::EdgeHashSet(base_path, log2_initial_buffer_size) {}

  // Returns true if the insert actually happened.
  bool testing_insert(BraceletTraceEdge edge) {
    auto *entry = &canonical[edge];
    bool was_inserted = *entry == 0;
    *entry = 1;
    bool ds_under_test_thinks_inserted = add_edge(edge);
    assert(ds_under_test_thinks_inserted == was_inserted);
    return was_inserted;
  }

  void check() {
    check_number++;
    size_t saw_edges = 0;
    for (size_t i = 0; i < edge_count; i++) {
      auto edge = edges[i];
      if (!edge_present(edge))
        continue;
      saw_edges++;
      IndexSequence iseq(*this, edge);
      while (true) {
        if (i == iseq())
          break;
      }
      auto *entry = &canonical[edge];
      assert(*entry != 0 && "edge not in canonical");
      assert(*entry != check_number && "duplicte edge");
      *entry = check_number;
    }
    assert(saw_edges == inserted_edges);
    assert(canonical.size() == inserted_edges);
  }

private:
  absl::flat_hash_map<CanonicalEdge, uint64_t> canonical;
  uint64_t check_number = 2;
};
} // namespace

int main() {
  char tmp[] = "/tmp/test_edge_hash_set.XXXXXX";
  if (mkdtemp(tmp) == nullptr) {
    perror("mkdtemp()");
    abort();
  }
  {
    TestingEdgeHashSet ths(tmp, 1);
    for (uint64_t i = 0; i < 64; i++) {
      ths.testing_insert({2, i});
      ths.testing_insert({2, i});
    }
  }
  std::vector<double> fresh_p_per_trial;
  fresh_p_per_trial.reserve(64);
  for (size_t i = 0; i < 32; i++) {
    fresh_p_per_trial.push_back(0.1);
  }
  for (size_t i = 0; i < 16; i++) {
    fresh_p_per_trial.push_back(0.5);
  }
  for (size_t i = 0; i < 16; i++) {
    fresh_p_per_trial.push_back(0.75);
  }
  for (size_t trial = 0; trial < fresh_p_per_trial.size(); trial++) {
    std::cerr << "Trial [" << fresh_p_per_trial[trial] << "] " << (trial + 1)
              << "/" << fresh_p_per_trial.size() << "\n";
    std::bernoulli_distribution generate_fresh(fresh_p_per_trial[trial]);
    TestingEdgeHashSet hs(tmp, 1);
    std::vector<BraceletTraceEdge> inserted;
    std::mt19937_64 rng(trial);
    constexpr size_t NUM_INSERTIONS = 1000000;
    for (size_t i = 0; i < NUM_INSERTIONS; i++) {
      if (i % 32768 == 0) {
        hs.check();
      }
      BraceletTraceEdge edge =
          (inserted.empty() || generate_fresh(rng))
              ? ((BraceletTraceEdge){rng() << 1, rng()})
              : inserted[std::uniform_int_distribution<size_t>(
                    0, inserted.size() - 1)(rng)];
      if (hs.testing_insert(edge))
        inserted.push_back(edge);
    }
    hs.check();
  }
  return 0;
}
