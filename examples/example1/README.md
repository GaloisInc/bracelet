# BRACELET Example 1

This directory contains a small file-processing application used to demonstrate
BRACELET's vulnerability triage and CI integration.

## Build and analyze

Follow the [environment setup](../../doc/src/getting-started.md),
then use the [reachability walkthrough](../../doc/src/example1.md) to
build this application with the BRACELET toolchain, run its tests, capture a
snapshot, and analyze the included vulnerability data.

## Program

The application uses libmagic to identify a file's MIME type and dispatches to
text, XML, or gzip handlers. Sample inputs are stored in `example-input/`.

```bash
./build/compressor --file ./example-input/input.txt.gz
```

## Vulnerabilities

`vulnerabilities.json` describes issues in zlib, libxml2, and libmagic. The
checked-in application reaches the libmagic and zlib functions. The libxml2
pattern-matching issue is unreachable because that API is unused.
