#!/usr/bin/env nix-shell
#!nix-shell --pure -i python3 ../shell.nix
# ruff: noqa: EXE005

"""
Turn output into datalog call graph facts
"""

import csv
import sys
from collections import defaultdict
from pathlib import Path

from bracelet_scripts.bracelet_reachability.cg_lib import (
    CallGraph,
    load_table,
    normal_symbol,
)


def build_callsite_table(pth: Path) -> dict[str, str]:
    cs = {}
    with pth.open() as f:
        for row in csv.reader(f, delimiter="\t"):
            cs[row[1]] = row[0]
    return cs


def dump_callgraph(
    pth: Path,
    cg: CallGraph,
    input_enclosing_func_for_local: Path,
    output: Path | None = None,
) -> Path:
    node_instructions = defaultdict(list)
    fast_instruction_file = pth / "FastNodeInstructions.facts"
    instruction_file = (
        fast_instruction_file
        if fast_instruction_file.stat().st_size > 0
        else pth / "NodeInstructions.facts"
    )
    for addr, node in load_table(instruction_file):
        node_instructions[node].append(addr)

    enclosing_table = build_callsite_table(input_enclosing_func_for_local)

    cg_path = pth / "CallGraph.csv" if output is None else output / "CallGraph.csv"
    with open(cg_path, "w") as output_file:
        for k, v in cg.call_graph_edges.items():
            normal_callees = [
                callee for callee in v if normal_symbol(cg.pretty(callee))
            ]
            if len(normal_callees) == 0:
                print(f"No concrete callees for {cg.pretty(k)}", file=sys.stderr)
            addrs = node_instructions[k]
            if len(addrs) == 0:
                print(f"No addresses for callsite {cg.pretty(k)}", file=sys.stderr)
            for c in normal_callees:
                if k in enclosing_table:
                    func = enclosing_table[k]
                    for addr in addrs:
                        print(f"{func}\t{addr}\t{c}", file=output_file)
                else:
                    print(f"No enclosing function {cg.pretty(k)}", file=sys.stderr)

        return cg_path


if __name__ == "__main__":
    pth = Path(".")
    cg = CallGraph(pth)
    # should have merged locals in working directory
    dump_callgraph(pth, cg, pth / "EnclosingFunctionForLocal.facts")
