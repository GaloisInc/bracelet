#!/usr/bin/env nix-shell
#!nix-shell -i python3 --pure ../shell.nix
# ruff: noqa: EXE005

from pathlib import Path
from shutil import copyfileobj

from tqdm import tqdm

MAPPING = {
    "Dlsym.facts": "Dlsym.facts",
    "DebugTable.facts": "DebugTable.facts",
    "NodeInstructions.facts": "NodeInstructions.facts",
    "FastNodeInstructions.facts": "FastNodeInstructions.facts",
    "Starred.facts": "PreciousStarred.csv",
    "ValueName.facts": "PreciousValueName.csv",
    "SbomTable.facts": "SbomTable.facts",
    "IsAlloca.facts": "PreciousIsAlloca.csv",
    "IsSymbol.facts": "IsSymbol.facts",
    "ArgumentDefinition.facts": "PreciousSubArgumentDefinition.csv",
    "ArgumentSupply.facts": "PreciousSubArgumentSupply.csv",
    "Assign.facts": "PreciousSubAssign.csv",
    "Call.facts": "PreciousSubCall.csv",
    "Load.facts": "PreciousSubLoad.csv",
    "Return.facts": "PreciousSubReturn.csv",
    "Store.facts": "PreciousSubStore.csv",
    "EnclosingFunctionForLocal.facts": "EnclosingFunctionForLocal.facts",
}

OPTIONAL = {"FastNodeInstructions.facts"}


def combine_using_mapping(pth: Path, outdir: Path, mapping: dict[str, str]) -> None:
    functions = list(pth.glob("0x*"))

    pb = tqdm(total=len(functions) * len(MAPPING))
    outdir.mkdir(exist_ok=True)
    for k, v in mapping.items():
        with (outdir / k).open("wb") as f_dst:
            for f in functions:
                read_pth = f / v
                if v not in OPTIONAL or read_pth.exists():
                    with read_pth.open("rb") as f_src:
                        copyfileobj(f_src, f_dst)
                        pb.update(1)


if __name__ == "__main__":
    pth = Path(".")
    outdir = pth / Path("output")
    combine_using_mapping(pth, outdir, MAPPING)
