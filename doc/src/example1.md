# Example 1 Walkthrough

Example 1 is a CLI file processor in
[`examples/example1`](../../../examples/example1/). It uses libmagic to detect
a file's MIME type and dispatches through an indirect call to a text, XML, or
gzip handler.

The example demonstrates how to build an application with BRACELET metadata,
capture its runtime dependencies, and classify reported vulnerabilities using
a reconstructed callgraph.

## Build

Enter the development environment described in
[Getting Started](./getting-started.md), then configure the project:

```bash
cd examples/example1

cmake -S . -B build -G Ninja \
  -DVCPKG_TARGET_TRIPLET=x64-linux-braceletnodbg \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$BRACELET_TOOLCHAIN_FILE" \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_TOOLCHAIN_FILE"

cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The `x64-linux-braceletnodbg` triplet builds the application and its pinned
vcpkg dependencies with BRACELET metadata, but omits optional names and source
locations used to make low-level analysis output easier to read.

Run the gzip input:

```bash
./build/compressor --file ./example-input/input.txt.gz
```

The output should include:

```text
Retrieved mimetype: application/gzip
Header listed timestamp: 0
Finished inflate call current total: 43
```

## Capture a snapshot

The snapshot records the process core and copies its executable and loaded
shared libraries into a sysroot:

```bash
python -m bracelet_scripts.snapshot_handler snapshot -- \
  ./build/compressor --file ./example-input/input.txt.gz
```

The destination directory must not already exist. Snapshotting requires a
native x86-64 Linux process. Docker Desktop with Rosetta produces an
incompatible ARM64 translator core.

If GDB reports a ptrace permission error, temporarily allow same-user
attachment:

```bash
echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope
```

## Run reachability analysis

Generate the callgraph and classify the vulnerability reports:

```bash
python -m bracelet_scripts.entrypoint \
  --bracelet-edges bracelet-edges \
  snapshot \
  --vuln-json vulnerabilities.json \
  --run-cg-filter \
  --save-callgraph callgraph.csv | tee result.json
```

The complete Bracelet result is printed and saved to `result.json`.

The reports cover:

| Vulnerability | Package | Expected result |
| --- | --- | --- |
| CVE-2022-37434 | zlib | Potentially reachable |
| CVE-2022-48554 | libmagic | Potentially reachable |
| CVE-2025-27113 | libxml2 | Unreachable |

The zlib path reaches `inflateGetHeader` through `ZlibCallback::processFile`.
The libmagic path reaches `file_copystr` through MIME detection. The libxml2
pattern-matching issue is unreachable because the application does not call
the affected API.

The optional SVF analysis can be enabled with:

```bash
python -m bracelet_scripts.entrypoint \
  --bracelet-edges bracelet-edges \
  snapshot \
  --vuln-json vulnerabilities.json \
  --run-cg-filter \
  --svf-pointer-analysis \
  --bracelet-points-to bracelet-points-to \
  --svf-llvm "$SVF_LLVM_PATH" \
  --svf-clang "$SVF_CLANG_PATH" \
  --svf-path "$SVF_PATH"
```

SVF is unlikely to terminate on this example in a reasonable time.

## GitLab pipeline

The pipeline is defined in `examples/example1/ci/gitlab-ci.yml` and included
by the repository's root pipeline. It has three jobs:

1. `example1-build` builds the application and runs all sample-input tests.
2. `example1-snapshot` captures the gzip execution.
3. `example1-analysis` creates the callgraph and prints the classifications.

The analysis job retains `result.json` and `callgraph.csv` as artifacts.
