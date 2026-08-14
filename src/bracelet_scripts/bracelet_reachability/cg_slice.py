import argparse
from pathlib import Path

from bracelet_scripts.bracelet_reachability.cg_lib import CallGraph


def main() -> None:
    prsr = argparse.ArgumentParser()
    prsr.add_argument("output_dir", type=Path)
    prsr.add_argument("--cg-name", type=str, required=True)
    prsr.add_argument("--target-symbol", required=True, type=str)
    prsr.add_argument("--dot-output", required=True, type=Path)

    args = prsr.parse_args()
    cg = CallGraph(args.output_dir, combined_call_name=args.cg_name)
    target_addr = cg.InvDebugTable[args.target_symbol]
    main_addr = cg.InvDebugTable["main"]
    cg.print_pretty_graph(cg.slice_between(main_addr, target_addr), args.dot_output)


if __name__ == "__main__":
    main()
