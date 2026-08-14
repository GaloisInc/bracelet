#!/usr/bin/env python3
import base64
import os
import sys
from hashlib import sha256
from pathlib import Path

FILES = [
    "dlsym_runtime.bc",
    "libbracelet_pointsto_trace_runtime.so",
    "libbracelet_reachability.so",
]

if __name__ == "__main__":
    bin = Path(os.environ["MESON_INSTALL_PREFIX"]) / "bin"
    dry_run = "MESON_INSTALL_DRY_RUN" in os.environ
    quiet = "MESON_INSTALL_QUIET" in os.environ
    for f in FILES:
        src = bin / f
        assert src.exists(), str(src)
        h = base64.urlsafe_b64encode(sha256(src.read_bytes()).digest()).decode("ascii")[
            0:32
        ]
        dst = src.with_stem(f"{src.stem}-{h}")
        if not quiet:
            print(f"Symlinking {src} to {dst}", file=sys.stderr)
        if not dry_run:
            src.rename(dst)
            src.symlink_to(dst.relative_to(src.parent))
