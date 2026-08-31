#!/usr/bin/env python3
"""Extract concretized bytes from screach log and write a PoV gzip file.

Usage:
    python3 make-pov.py <logfile> [-o pov.gz]

Parses the "Concretized values:" section from screach verbose output,
extracts the first file_content/sym_bytes block, and writes it as a raw
gzip file. Then optionally runs the compressor binary on it.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path


def extract_bytes(logpath: Path) -> bytes:
    """Extract concretized stdin bytes from a screach log.

    Parses the "Concretized filesystem:" section, looking for the "stdin"
    block of hex bytes.
    """
    text = logpath.read_text()

    # Find "Concretized filesystem:" section, then the stdin block
    match = re.search(
        r"Concretized filesystem:\s*\n\s*stdin\s*\n(.*?)(?:\n\[|\n\S|\Z)",
        text, re.DOTALL,
    )
    if not match:
        sys.exit("Could not find 'Concretized filesystem: stdin' in log")

    block = match.group(1)
    hex_bytes = []
    for line in block.strip().split("\n"):
        stripped = line.strip()
        if re.match(r'^[0-9a-fA-F]{2}(\s+[0-9a-fA-F]{2})*$', stripped):
            hex_bytes.extend(stripped.split())
        else:
            break

    if not hex_bytes:
        sys.exit("Could not parse hex bytes from stdin block")

    result = bytes(int(b, 16) for b in hex_bytes)
    print(f"  {len(result)} bytes, starts with {result[:12].hex(' ')}")
    return result


def main():
    parser = argparse.ArgumentParser(description="Extract PoV from screach log")
    parser.add_argument("logfile", type=Path, help="screach verbose log file")
    parser.add_argument("-o", "--output", type=Path, default=Path("pov.gz"),
                        help="output file (default: pov.gz)")
    parser.add_argument("--run", action="store_true",
                        help="run the binary on the PoV file")
    parser.add_argument("--binary", type=Path, default=Path("build/extract"),
                        help="extract binary path")
    args = parser.parse_args()

    data = extract_bytes(args.logfile)
    args.output.write_bytes(data)
    print(f"Wrote {len(data)} bytes to {args.output}")
    print(f"Header: {data[:12].hex(' ')}")

    if args.run:
        # extract reads from stdin
        cmd = [str(args.binary)]
        print(f"\nRunning: {' '.join(cmd)} < {args.output}")
        with open(args.output, "rb") as f:
            result = subprocess.run(cmd, stdin=f, timeout=10, capture_output=True, text=True)
        print(f"stdout: {result.stdout}")
        print(f"stderr: {result.stderr}")
        print(f"Return code: {result.returncode}")
        if result.returncode < 0:
            import signal
            sig = -result.returncode
            try:
                signame = signal.Signals(sig).name
            except (ValueError, AttributeError):
                signame = f"signal {sig}"
            print(f"Process killed by {signame}")


if __name__ == "__main__":
    main()
