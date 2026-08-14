#!/usr/bin/env python3
# mypy: disable-error-code="assignment,arg-type,operator,attr-defined,var-annotated"

import itertools
import subprocess
import sys
import tempfile
from collections import defaultdict
from json import dumps as q
from pathlib import Path

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    rules = subprocess.check_call(
        [Path("./build/bin/bracelet-edges").resolve(), sys.argv[1]]
        + [Path(x).resolve() for x in sys.argv[2:]],
        cwd=tmp,
    )
    (tmp / "rules.dl").write_bytes(
        subprocess.check_output(["python3", "gen_rules.py"]) + b"\n\n.output sub"
    )
    subprocess.check_call(["souffle", "rules.dl"], cwd=tmp)
    debug_names = {}
    with (tmp / "DebugTable.facts").open() as f:
        for line in f:
            line = line.strip()
            if len(line) == 0:
                continue
            node, *label = line.split("\t")
            debug_names[node] = "\t".join(label)
    TABLES = {}
    for p in tmp.iterdir():
        if p.name in (
            "DebugTable.facts",
            "rules.dl",
            "IsAlloca.facts",
            "IsLocal.facts",
            "EnclosingFunction.facts",
        ):
            continue
        table = set()
        delim = "\t"  # if p.name.endswith(".facts") else ","
        with p.open() as f:
            for line in f:
                line = line.strip()
                if len(line) == 0:
                    continue
                table.add(tuple(line.split(delim)))
        TABLES[p.stem] = table

COLORSCHEMES = [
    ("set39", 9),
    ("spectral9", 9),
    ("set19", 9),
]
COLORS = list(
    itertools.chain.from_iterable(
        [f"style=filled colorscheme={name} fillcolor={i+1}" for i in range(count)]
        for name, count in COLORSCHEMES
    )
)
# component_colors = dict(zip(set(component_assignments.values()), itertools.chain(COLORS, itertools.repeat(""))))

SUB_CHILDREN = defaultdict(set)
SUB_PARENTS = defaultdict(set)
for to, from_ in TABLES["sub"]:
    SUB_CHILDREN[from_].add(to)
    SUB_PARENTS[to].add(from_)


class_names: dict[str, str] = {}


def class_name(node: str) -> str:
    if node not in class_names:
        class_names[node] = f"class-{len(class_names)}"
    return class_names[node]


with open("graph.dot", "w") as f:
    print("digraph X{", file=f)

    for k, v in debug_names.items():
        color = ""  # component_colors[component_assignments[k]]
        v = v.split(" (rnd=")[0]
        classes = " ".join(
            ["PARENT" + class_name(x) for x in SUB_PARENTS[k]]
            + ["CHILD" + class_name(x) for x in SUB_CHILDREN[k]]
        )
        print(
            f"{q(k)} [label={q(v)} {color} class={q(classes)} id={q(class_name(k))}];",
            file=f,
        )

    for t, rows in TABLES.items():
        if t == "sub":
            continue
        for dst, src, *maybe_num in rows:
            if len(maybe_num) > 0:
                label = f"{t} ({maybe_num[0]})"
            else:
                label = t
            print(f"{q(src)} -> {q(dst)} [label={q(label)}];", file=f)

    print("}", file=f)

HTML = """
<style>
.highlight-parent {
    fill: orange;
}
.highlight-child {
    fill: purple;
}
</style>
<script><![CDATA[
window.addEventListener("click", function (e) {
    const target = e.target.closest(".node");
    if (!target) return;
    for(const x of document.querySelectorAll(".highlight-parent"))
        x.classList.remove("highlight-parent");
    for(const x of document.querySelectorAll(".highlight-child"))
        x.classList.remove("highlight-child");
    for(const x of document.querySelectorAll(".CHILD" + target.id))
        x.classList.add("highlight-child");
    for(const x of document.querySelectorAll(".PARENT" + target.id))
        x.classList.add("highlight-parent");
});
]]></script>
"""

Path("graph.svg").write_text(
    subprocess.check_output(["dot", "-Tsvg", "graph.dot"])
    .decode("ascii")
    .replace("</svg>", f"{HTML}</svg>")
)
