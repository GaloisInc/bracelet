# Developer documentation

## Build

Building our LLVM requires:

```bash
apt-get install swig python3-dev lld ninja ccache
```

Then, to build:

```bash
# NOTE: you need to point LLVM_CONFIG at the llvm-config binary from the install
# LLVM prefix (not the raw build directory)
env LLVM_CONFIG=<path to your llvm-config binary> uv run --dev meson setup build --prefix=<your install prefix>
uv run --dev ninja -C build
uv run --dev ninja -C build install # if you want to install to your prefix (optional)
```

The pass can also be built with upstream LLVM for CI checks; see
[Building With Upstream LLVM](../getting-started.md#building-with-upstream-llvm).

If you want to build an optimized version of our tools, pass
`--buildtype=debugoptimized` to the `meson setup` command described above.

 `build/bracelet-cc.sh` or `<prefix>/bin/bracelet-cc.sh` is the clang wrapper script

## Tests

<!-- TODO: Documentation about the test suite-->

To run tests:

```bash
uv run --dev pytest src/
uv run --dev ninja -C build test
```

## Table of contents

- **`src/Edges`**: code for reading and writing graph data to/from binaries and coredump
- **`src/bracelet_scripts`**: Our python code for reachability
- **`src/bracelet_scripts/entrypoint.py`**: The script that gets invoked in CI for our analysis job
- **`src/bracelet_scripts/bracelet_reachability`**: The original version of our points-to analysis (using soufflé).
- **`src/bracelet_scripts/bracelet_reachability/apply_overrides.py`**: Apply the overrides in overrides.c to an existing graph
- **`src/bracelet_scripts/bracelet_reachability/cg_lib.py`**: A library to operate on callgraph edges
- **`src/bracelet_scripts/bracelet_reachability/gen_rules.py`**: Generate the datalog rules for our full/slow points-to analysis
- **`src/bracelet_scripts/bracelet_reachability/gen_rules_simple.py`**: Generate the datalog rules for our super simple points-to analysis
- **`src/bracelet_scripts/points_to`**: V2 of our points-to code. This is the first version that emits C code for SVF to process
- **`src/bracelet_scripts/tracing.py`**: Library to parse trace data from python
- **`src/bracelet-edges`**: `bracelet-edges` tool to emit CSV data for our graph edges (consumed by our python scripts)
- **`src/BraceletReachability`**: Our LLVM pass to embed edges into an LLVM module
- **`src/BraceletReachability/dlsym_runtime.c`**: Code that's used to support our tracking/instrumentation of dlsym()
- **`src/BraceletReachability/test_dlsym`**: Tests for our dlsym instrumentation. Because this is only testing the LLVM pass, this python code is equally applicable to the C++ (V3) points-to. (That is, because it checks that the BraceletReachability pass+runtime work properly, its tests are independent of which points-to code consumes the edges.)
- **`src/BraceletReachability/test_tracing`**: Tests for our indirect callee-only tracing instrumentation. Because this is only testing the LLVM pass, this python code is equally applicable to the C++ (V3) points-to. (That is, because it checks that the BraceletReachability pass+runtime work properly, its tests are independent of which points-to code consumes the edges.)
- **`src/ObjectParsing`**: Parse section data out of coredumps and executables
- **`src/PointsTo`**: Our V3 points-to analysis. This parses edge data out of core dumps and emits C code for SVF to process. Unlike the V2 points-to analysis, this approach is much faster and also supports comparing full trace data against SVF's output.
- **`src/Result`**: A unified Rust-style `Result` type
- **`src/RuntimeFormat`**: Specifies data structures and functions that are shared between writers at compile time, runtime libraries at runtime, and readers at analysis/post-coredump time
- **`src/Subprocess`**: A utility library to launch subprocesses
- **`src/tempfile`**: A utility library to create temporary directories
- **`src/Tracing`**: Our tracing runtime to support tracing in our LLVM pass

## Profiling

The `profile` directory contains scripts for comparing compile time between
clang, bracelet-clang, and bracelet-clang with metadata enabled. The README
describes how to use those scripts.

The meson option `profile` can be set to to true (e.g. `uv run --dev meson
setup profile-build  --prefix=$(pwd)/prefix-profile  --buildtype=debugoptimized
-Dprofile=true`)  to launch bracelet-cc and bracelet-cxx with callgrind. This
build can be used with the scripts in profiling to get profiling data sufficient
for analysis in qcachegrind.

Note: This information is call counts and not timing information so can be
biased.
