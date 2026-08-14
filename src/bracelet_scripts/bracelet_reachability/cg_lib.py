from __future__ import annotations

import itertools
import sys
from collections import defaultdict
from collections.abc import Iterator
from pathlib import Path

import networkx as nx


def load_table(p: Path) -> Iterator[list[str]]:
    with open(p) as f:
        for line in f:
            line = line.strip()
            if line == "":
                continue
            yield line.split("\t")


class CallGraph:
    def __init__(self, base: Path, combined_call_name: str | None = None) -> None:
        combined_call_table = (
            combined_call_name
            if combined_call_name is not None
            else "PreciousSubCall.csv"
        )
        self.DebugTable = dict(load_table(base / "DebugTable.facts"))
        self.InvDebugTable = {v: k for k, v in self.DebugTable.items()}
        self.call_graph_edges: defaultdict[str, set[str]] = defaultdict(set)
        for k, v, _ in load_table(base / combined_call_table):
            self.call_graph_edges[k].add(v)
        self.G: nx.DiGraph[str] = nx.DiGraph()
        for caller, callee, kind in load_table(base / combined_call_table):
            self.G.add_edge(caller.split(":")[0], callee.split(":")[0], kind=kind)

    # forward slice form x, backward slice from y
    def slice_between(self, x: str, y: str) -> nx.DiGraph[str]:
        slice = nx.descendants(self.G, x).intersection(nx.ancestors(self.G, y))
        slice.add(x)
        slice.add(y)
        return self.G.subgraph(slice)  # type: ignore[return-value]

    def print_pretty_graph(self, G: nx.DiGraph[str], pth: Path) -> None:
        mpping = {x: self.pretty(x) for x in G.nodes}
        rlablel = nx.relabel_nodes(G, mpping)
        nx.drawing.nx_pydot.write_dot(rlablel, pth)

    def print_path_between(self, x: str, y: str) -> str | None:
        if x not in self.G.nodes or y not in self.G.nodes:
            if x not in self.G.nodes:
                print(f"Warning: source {x} not in callgraph", file=sys.stderr)
            if y not in self.G.nodes:
                print(f"Warning: destination {y} not in callgraph", file=sys.stderr)
            return None

        try:
            KIND_WEIGHTS = {
                "Direct": 1,
                "Indirect": 10,
                "Conservative": 100,
                "Unknown": 100,
            }

            def get_weight(_x: str, _y: str, attrs: dict[str, str]) -> int:
                return KIND_WEIGHTS[attrs["kind"]]

            p = nx.shortest_path(self.G, x, y, weight=get_weight)

            KIND_ARROWS = {
                "Direct": "--->",
                "Indirect": "-I->",
                "Conservative": "-?->",
                "Unknown": "-?->",
            }

            return "".join(
                [
                    f" {KIND_ARROWS[self.G[u][v]['kind']]} {self.pretty(v)}"
                    for u, v in itertools.pairwise(p)
                ]
            )
        except nx.NetworkXNoPath:
            print(f"Warning: No path between {x} and {y} in callgraph", file=sys.stderr)
            return None

    def print_path_from_main(
        self, target: str, waypoints: list[str] | None = None
    ) -> str | None:
        if waypoints is None:
            waypoints = []
        main_addr = next((k for k, v in self.DebugTable.items() if v == "main"), None)
        if main_addr is None:
            return None

        cur = main_addr
        output = self.pretty(cur)
        to_reach = waypoints + [target]
        while to_reach:
            waypoint = to_reach.pop(0)
            next_path = self.print_path_between(cur, waypoint)
            if next_path is None:
                return None
            cur = waypoint
            output += next_path

        return output

    def pretty(self, x: str) -> str:
        prefix = ""
        while x.startswith(("*", "$")):
            prefix += x[0]
            x = x[1:]
        return prefix + self.DebugTable.get(x, x)


def normal_symbol(x: str) -> bool:
    return " -> " not in x and "*" not in x and x[0] != "."
