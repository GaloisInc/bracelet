#!/usr/bin/env nix-shell
#!nix-shell --pure -i python3 ../shell.nix
# ruff: noqa: EXE005
# mypy: disable-error-code="assignment,operator,attr-defined,var-annotated,index"

import subprocess
import sys
import tempfile
from collections.abc import Iterator
from pathlib import Path

from tqdm import tqdm

LLVM = Path(__file__).resolve().parent.parent


def load_table(p: Path) -> Iterator[list[str]]:
    with open(p) as f:
        for line in f:
            line = line.strip()
            if line == "":
                continue
            yield line.split("\t")


def apply_overrides(bracelet_edges: Path, bracelet_clang: Path) -> None:
    with tempfile.TemporaryDirectory() as overrides:
        overrides = Path(overrides)
        overrides_so = overrides / "overrides.so"
        subprocess.check_call(
            [
                bracelet_clang,
                "-shared",
                "-o",
                overrides_so,
                LLVM / "bracelet_reachability/overrides.c",
            ]
        )
        subprocess.check_call([bracelet_edges, "overrides.so"], cwd=overrides)

        overrides = {
            dict(load_table(f / "DebugTable.facts"))[f.name]: f
            for f in tqdm(list(overrides.glob("0x*")))
        }

        override_mapping = {}
        override_reverse_mapping = {}

        for dt_path in tqdm(list(Path(".").glob("0x*/DebugTable.facts"))):
            for addr, name in load_table(dt_path):
                if name in overrides:
                    if name not in ["memcmp", "bcmp"]:
                        assert override_mapping.get(addr) in (None, name)
                    assert override_reverse_mapping.get(name) in (None, addr)
                    override_mapping[addr] = name
                    override_reverse_mapping[name] = addr

        override_replacements = [
            (overrides[name].name, addr, name)
            for addr, name in override_mapping.items()
        ]

        print("Overrides:", file=sys.stderr)
        for orig_addr, new_addr, name in override_replacements:
            print(f" - {name}: {orig_addr} => {new_addr}", file=sys.stderr)

        for addr, name in tqdm(list(override_mapping.items())):
            dst = Path(addr)
            dst.mkdir()  # if it exists, we're overridding an already defined function
            for f in overrides[name].iterdir():
                txt = f.read_text()
                for orig, new, _ in override_replacements:
                    txt = txt.replace(orig, new)
                (dst / f.name).write_text(txt)


if __name__ == "__main__":
    apply_overrides(LLVM / "build/bin/bracelet-edges", Path("bracelet-clang"))
