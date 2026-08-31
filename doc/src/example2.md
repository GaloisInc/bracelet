# Example 2 Walkthrough

Example 2 is a CLI file processor in
[`examples/example2`](../../../examples/example2/). It uses libmagic to detect
a file's MIME type and dispatches through an indirect call to a text, XML, or
gzip handler.

The example demonstrates how to build an application with BRACELET metadata,
capture its runtime dependencies, and classify reported vulnerabilities using
a reconstructed callgraph.

Example 2 is substantially similar to [Example 1](example1.md), so this
walkthrough has less commentary. See the Example 1 walkthrough for additional
discussion of each step.

## Build

Enter the development environment described in
[Getting Started](./getting-started.md), then configure the project:

```bash
cd examples/example2

cmake -S . -B build -G Ninja \
  -DVCPKG_TARGET_TRIPLET=x64-linux-braceletnodbg \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$BRACELET_TOOLCHAIN_FILE" \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_TOOLCHAIN_FILE"

cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run the program:

```bash
printf 'hello\n' | ./build/extract
```

The program should output `hello`.

## Capture a snapshot

Take a snapshot:

```bash
printf 'hello\n' | \
  python -m bracelet_scripts.snapshot_handler snapshot -- \
  ./build/extract
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

The reachability results are:

| Vulnerability | Package | Result |
| --- | --- | --- |
| CVE-2022-37434 | zlib | Potentially reachable |
| CVE-2022-48554 | libmagic | Potentially reachable |
| CVE-2025-27113 | libxml2 | Unreachable |

The zlib path reaches `inflateGetHeader` through `handle_gzip`. The libmagic
path reaches `file_copystr` through `detect_mime` and `magic_buffer`. The
libxml2 pattern-matching issue is unreachable because `xmlPatMatch` is not in
the generated callgraph.

<!--

TODO: Analysis with Screach

The `justfile` records how to run Screach, but there has been some regression.

-->
