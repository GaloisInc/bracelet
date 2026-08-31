#!/usr/bin/env python3
"""Generate a callgraph CSV from an ELF binary's relocations and direct calls.

Output format (tab-separated, matching screach --callgraph expectations):
    caller_func_addr\tcallsite_addr\tcallee_func_addr

All addresses include the load offset (default 0x10000000 for screach on
raw ELF binaries).

Usage:
    python3 gen-callgraph.py <binary> [--load-offset 0x10000000] [-o callgraph.csv]
"""

import argparse
import bisect
import re
import struct
import subprocess
import sys


def get_functions(binary: str) -> dict[int, str]:
    """Return {addr: name} for all text symbols."""
    result = subprocess.run(["nm", binary], capture_output=True, text=True, check=True)
    funcs = {}
    for line in result.stdout.splitlines():
        m = re.match(r"^([0-9a-f]+)\s+[TtWw]\s+(.+)$", line)
        if m:
            funcs[int(m.group(1), 16)] = m.group(2)
    return funcs


def find_containing_func(sorted_addrs: list[int], addr: int) -> int | None:
    """Find the function containing addr via binary search."""
    idx = bisect.bisect_right(sorted_addrs, addr) - 1
    if idx >= 0:
        return sorted_addrs[idx]
    return None


def get_reloc_edges(binary: str, funcs: dict[int, str], sorted_addrs: list[int]) -> list[tuple[int, int, int]]:
    """Extract (caller_func, callsite, callee_func) from PLT32 relocations."""
    result = subprocess.run(
        ["readelf", "-rW", binary], capture_output=True, text=True, check=True
    )
    edges = []
    for line in result.stdout.splitlines():
        m = re.match(
            r"^([0-9a-f]+)\s+[0-9a-f]+\s+R_X86_64_PLT32\s+([0-9a-f]+)\s+", line
        )
        if m:
            callsite = int(m.group(1), 16) - 1
            target = int(m.group(2), 16)
            if target not in funcs:
                continue
            containing = find_containing_func(sorted_addrs, callsite)
            if containing is not None:
                edges.append((containing, callsite, target))
    return edges


def get_text_section(binary: str) -> tuple[int, int, bytes]:
    """Return (vaddr, size, data) for the .text section."""
    result = subprocess.run(
        ["readelf", "-S", "-W", binary], capture_output=True, text=True, check=True
    )
    for line in result.stdout.splitlines():
        # Match: [15] .text  PROGBITS  addr  offset  size ...
        m = re.match(
            r"\s*\[\s*\d+\]\s+\.text\s+PROGBITS\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)",
            line,
        )
        if m:
            addr = int(m.group(1), 16)
            offset = int(m.group(2), 16)
            size = int(m.group(3), 16)
            with open(binary, "rb") as f:
                f.seek(offset)
                data = f.read(size)
            return (addr, size, data)
    raise RuntimeError("Could not find .text section")


def get_direct_call_edges(binary: str, funcs: dict[int, str], sorted_addrs: list[int]) -> list[tuple[int, int, int]]:
    """Extract direct call edges by scanning for e8 (relative call) instructions."""
    func_set = set(funcs.keys())
    text_addr, text_size, text_data = get_text_section(binary)
    edges = []
    i = 0
    while i < len(text_data) - 4:
        if text_data[i] == 0xe8:  # relative call
            rel = struct.unpack_from("<i", text_data, i + 1)[0]
            callsite = text_addr + i
            target = callsite + 5 + rel
            if target in func_set:
                containing = find_containing_func(sorted_addrs, callsite)
                if containing is not None:
                    edges.append((containing, callsite, target))
            i += 5
        else:
            i += 1
    return edges


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", help="ELF binary to analyze")
    parser.add_argument(
        "--load-offset",
        default="0x10000000",
        help="Load offset applied by screach (default: 0x10000000)",
    )
    parser.add_argument("-o", "--output", default="callgraph.csv", help="Output file")
    args = parser.parse_args()

    offset = int(args.load_offset, 0)
    funcs = get_functions(args.binary)
    sorted_addrs = sorted(funcs.keys())

    reloc_edges = get_reloc_edges(args.binary, funcs, sorted_addrs)
    direct_edges = get_direct_call_edges(args.binary, funcs, sorted_addrs)

    # Deduplicate
    all_edges = sorted(set(reloc_edges + direct_edges))

    with open(args.output, "w") as f:
        for caller, site, callee in all_edges:
            f.write(f"0x{caller+offset:x}\t0x{site+offset:x}\t0x{callee+offset:x}\n")

    print(
        f"Generated {len(all_edges)} call edges "
        f"({len(reloc_edges)} reloc + {len(direct_edges)} direct, "
        f"{len(all_edges)} unique) -> {args.output}",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
