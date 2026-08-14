#include <fcntl.h>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#include "CLI/CLI.hpp"
#include "BraceletRuntimeStructs_c.h"
#include "absl/container/flat_hash_set.h"

bool operator==(const BraceletTraceEdge &e1, const BraceletTraceEdge &e2) {
  return e1.trace_site == e2.trace_site && e1.value == e2.value;
}
template <typename H> H AbslHashValue(H h, const BraceletTraceEdge &e) {
  return H::combine(std::move(h), e.trace_site, e.value);
}

int main(int argc, const char **argv) {
  // Dedeuplicate and remove zero/empty entries in trace files.
  // Python is slow to parse binary data, so running this simplify script first
  // speeds up the python parsing.
  CLI::App app{"Simplify traces"};

  std::string output = "";
  std::vector<std::string> inputs;
  app.add_option("-o,--output", output, "The file to write the output to")
      ->required();
  app.add_option("input", inputs)->required();

  CLI11_PARSE(app, argc, argv);

  absl::flat_hash_set<BraceletTraceEdge> trace_edges;
  for (const auto &input : inputs) {
    // We leak the mmap()'d memory because elements_by_stride might still point
    // at it.
    int fd = open(input.c_str(), O_RDONLY);
    if (fd < 0) {
      perror("open()");
      abort();
    }
    auto len = lseek(fd, 0, SEEK_END);
    if (len < 0) {
      perror("lseek()");
      abort();
    }
    void *mmap_out = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
    if (mmap_out == MAP_FAILED) {
      perror("mmap()");
      abort();
    }
    char *body = reinterpret_cast<char *>(mmap_out);
    for (size_t i = 0; i + sizeof(BraceletTraceEdge) <= static_cast<size_t>(len);
         i += sizeof(BraceletTraceEdge)) {
      BraceletTraceEdge e;
      memcpy(&e, &body[i], sizeof(e));
      if (e.trace_site != 0)
        trace_edges.insert(e);
    }
    close(fd);
  }
  FILE *f = fopen(output.c_str(), "w");
  if (f == NULL) {
    perror("fopen()");
    abort();
  }
  for (const auto &elem : trace_edges) {
    if (fwrite(&elem, 1, sizeof(BraceletTraceEdge), f) !=
        sizeof(BraceletTraceEdge)) {
      perror("fwrite()");
      abort();
    }
  }
  if (fflush(f) != 0) {
    perror("fflush()");
    abort();
  }
  fclose(f);
  return 0;
}
