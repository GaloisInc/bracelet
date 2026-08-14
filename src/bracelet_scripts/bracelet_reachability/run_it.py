#!/usr/bin/env nix-shell
#!nix-shell --pure --keep SKIP_OVERRIDES -i python3 ../shell.nix
# ruff: noqa: EXE005
# mypy: disable-error-code="arg-type,operator"

# Uses $PWD as the working directory for outputs.

import hashlib
import os
import subprocess
import sys
from pathlib import Path

from tqdm import tqdm

from bracelet_scripts.bracelet_reachability import apply_overrides, combine

ROOT = Path(__file__).resolve().parent.parent


def eprint(msg: object, *args: object) -> None:
    print(msg, *args, file=sys.stderr)


def rules(*args: str) -> Path:
    RULES = subprocess.check_output(
        ["python3", ROOT / "bracelet_reachability/gen_rules.py"] + list(args)
    )
    RULES_HASH = hashlib.sha256(RULES).hexdigest()
    RULES_DIR = ROOT / "rules"
    RULES_DIR.mkdir(exist_ok=True)
    RULES_BIN = RULES_DIR / f"rules-{RULES_HASH}"
    if not RULES_BIN.exists():
        eprint("Compiling souffle")
        rules_dl = RULES_DIR / f"rules-{RULES_HASH}.dl"
        rules_dl.write_bytes(RULES)
        subprocess.check_call(["souffle", "-jauto", "-o", RULES_BIN, rules_dl])
    return RULES_BIN


def run_on_path(
    pth: Path, output_path: Path, bracelet_edges: Path, bracelet_clang: Path
) -> None:
    output_path.mkdir(exist_ok=True)
    eprint("Applying overrides")
    if "SKIP_OVERRIDES" in os.environ:
        eprint("... skipping, as requested")
    else:
        apply_overrides.apply_overrides(bracelet_edges, bracelet_clang)
    eprint("Doing per-function run")
    subprocess.check_call(
        ["python3", ROOT / "bracelet_reachability/run_rules.py", rules("phase1")],
        cwd=pth,
    )
    eprint("Catting facts")
    with (output_path / "reachable.facts").open("w") as f:
        for fn in tqdm(pth.glob("0x*/function-name.txt")):
            name = fn.read_text().strip()
            if name == "main" or "bracelet_reachability_globals" in name:
                print(fn.parent.name, file=f)

    combine.combine_using_mapping(pth, output_path, combine.MAPPING)
    eprint("Doing Combined Run")
    subprocess.check_call([rules("phase2"), "-jauto"], cwd=output_path)
    eprint("Emitting callgraph")
    subprocess.check_call(
        ["python3", ROOT / "bracelet_reachability/datalog_callgraph_output.py"],
        cwd=output_path,
    )


if __name__ == "__main__":
    eprint("Dumping Edges")
    subprocess.check_call([ROOT / "build/bin/bracelet-edges"] + sys.argv[1:])
