#!/usr/bin/env nix-shell
#!nix-shell --pure -i python3 ../shell.nix
# ruff: noqa: EXE005

from pathlib import Path

from bracelet_scripts.bracelet_reachability.cg_lib import CallGraph, normal_symbol

cg = CallGraph(Path("."))
for k, v in cg.call_graph_edges.items():
    if len(v) == 1 and normal_symbol(cg.pretty(next(iter(v)))):
        continue
    callees = [cg.pretty(x) for x in sorted(v)]
    print(
        f"{cg.pretty(k)}\t\t"
        + ", ".join(callee for callee in callees if normal_symbol(callee))
        + "\t||\t"
        + ", ".join(callee for callee in callees if not normal_symbol(callee))
    )
