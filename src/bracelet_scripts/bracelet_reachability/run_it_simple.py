#!/usr/bin/env nix-shell
#!nix-shell --pure -i python3 ../shell.nix
# ruff: noqa: EXE005
# mypy: disable-error-code="arg-type,operator,assignment,call-overload"
import hashlib
import subprocess
import sys
from pathlib import Path
from shutil import copyfileobj

from tqdm import tqdm

ROOT = Path(__file__).resolve().parent.parent


def run_on_path(edges: Path, output: Path) -> None:
    rule_bin = rules()
    output.mkdir(exist_ok=True)
    for function in tqdm(list(edges.glob("0x*"))):
        is_globals = (
            "bracelet_reachability_globals"
            in (function / "function-name.txt").read_text()
        )
        for f in function.glob("*.facts"):
            with (
                (output / (f"Global{f.name}" if is_globals else f.name)).open(
                    "ab"
                ) as f_dst,
                f.open("rb") as f_src,
            ):
                copyfileobj(f_src, f_dst)
    with (
        (output / "reachable.facts").open("w") as f,
        (output / "FunctionName.facts").open("w") as names,
    ):
        for fn in tqdm(edges.glob("0x*/function-name.txt")):
            name = fn.read_text().strip()
            names.write(f"{fn.parts[-2]}\t{name}\n")
            if name == "main" or "bracelet_reachability_globals" in name:
                print(fn.parent.name, file=f)
    print("Running souffle", file=sys.stderr)
    subprocess.check_call([rule_bin, "-jauto"], cwd=output)


def rules(*args: str) -> Path:
    RULES = subprocess.check_output(
        ["python3", ROOT / "bracelet_reachability/gen_rules_simple.py"] + list(args)
    )
    RULES_HASH = hashlib.sha256(RULES).hexdigest()
    RULES_DIR = ROOT / "rules"
    RULES_DIR.mkdir(exist_ok=True)
    RULES_BIN = RULES_DIR / f"rules-{RULES_HASH}"
    if not RULES_BIN.exists():
        print("Compiling souffle", file=sys.stderr)
        rules_dl = RULES_DIR / f"rules-{RULES_HASH}.dl"
        rules_dl.write_bytes(RULES)
        subprocess.check_call(["souffle", "-jauto", "-o", RULES_BIN, rules_dl])
    return RULES_BIN


def main() -> None:
    RULES_BIN = rules()
    print("Dumping Edges")
    subprocess.check_call([ROOT / "build/bin/bracelet-edges"] + sys.argv[1:])
    print("Catting edges")
    output = Path("output")
    output.mkdir(exist_ok=True)
    for f in tqdm(list(Path(".").glob("0x*/*.facts"))):
        with (output / f.name).open("ab") as f_dst, f.open("rb") as f_src:
            copyfileobj(f_src, f_dst)
    with (
        (output / "reachable.facts").open("w") as f,
        (output / "FunctionNames.facts").open("w") as names,
    ):
        for fn in tqdm(list(Path(".").glob("0x*/function-name.txt"))):
            name = fn.read_text().strip()
            names.write(f"{fn.parts[-2]}\t{name}\n")
            if name == "main" or "bracelet_reachability_globals" in name:
                print(fn.parent.name, file=f)
    print("Running souffle", file=sys.stderr)
    subprocess.check_call([RULES_BIN, "-jauto"], cwd=output)


if __name__ == "__main__":
    main()
