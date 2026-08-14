import argparse
import sys
import tempfile
from pathlib import Path

from bracelet_scripts import edges_utils
from bracelet_scripts.bracelet_reachability import (
    cg_lib,
    datalog_callgraph_output,
    run_it_simple,
)


def main() -> None:
    prsr = argparse.ArgumentParser("Dump callgraph")
    prsr.add_argument("--bracelet-edges", default="bracelet-edges")
    prsr.add_argument("--pid", default=None)
    prsr.add_argument("--save-wdir", action="store_true", default=False)
    prsr.add_argument("snapshot")
    args = prsr.parse_args()

    with tempfile.TemporaryDirectory(delete=not args.save_wdir) as d:
        if args.save_wdir:
            print(f"Saving wdir {d}", file=sys.stderr)
        edges_utils.run_bracelet_edges(
            args.snapshot, args.pid, d, args.bracelet_edges, None, False
        )
        pth = Path(d)
        output_path = pth / "output"
        print(output_path, file=sys.stderr)
        run_it_simple.run_on_path(pth, output_path)
        result_path = datalog_callgraph_output.dump_callgraph(
            output_path,
            cg_lib.CallGraph(output_path, "callgraph.csv"),
            output_path / "EnclosingFunctionForLocal.facts",
        )
        print(result_path.read_text())


if __name__ == "__main__":
    main()
