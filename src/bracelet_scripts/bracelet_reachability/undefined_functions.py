#!/usr/bin/env nix-shell
#!nix-shell --pure -i python3 ../shell.nix
# ruff: noqa: EXE005
from collections.abc import Iterator
from pathlib import Path


def load_table(p: Path) -> Iterator[list[str]]:
    with open(p) as f:
        for line in f:
            line = line.strip()
            if line == "":
                continue
            yield line.split("\t")


DebugTable: dict[str, str] = {}

for dt in Path(".").glob("0x*/DebugTable.facts"):
    DebugTable |= dict(load_table(dt))

unknown: set[str] = set()
for calls in Path(".").glob("0x*/Call.facts"):
    for x in calls.read_text().split():
        sym = x.replace("*", "").split(":")[0]
        if not Path(sym).exists():
            unknown.add(sym)

for sym in sorted(unknown):
    print(DebugTable.get(sym, sym))
