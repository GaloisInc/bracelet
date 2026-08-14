#!/usr/bin/env nix-shell
#!nix-shell -i python3 --pure ../shell.nix
# ruff: noqa: EXE005
import subprocess
import sys
from multiprocessing import Pool
from pathlib import Path

from tqdm import tqdm


class RuleExecutionError(RuntimeError):
    pass


rules = Path(sys.argv[1]).resolve()


def background(entry: Path) -> Path | None:
    p = subprocess.Popen([rules, "-j1"], cwd=entry)
    try:
        rc = p.wait(timeout=2)
        if rc != 0:
            raise RuleExecutionError(f"{entry} failed!")
        return None
    except subprocess.TimeoutExpired:
        p.kill()
        p.wait()

        return entry


def main() -> None:
    ents = list(Path(".").glob("0x*"))
    slow: list[Path] = []
    with Pool() as p:
        for res in tqdm(p.imap(background, ents, chunksize=3), total=len(ents)):
            if res is not None:
                slow.append(res)

    for i, entry in enumerate(slow):
        print(f"SLOW ENTRY [{i}/{len(slow)}]: {entry}")
        subprocess.check_call([rules, "-jauto"], cwd=entry)


if __name__ == "__main__":
    main()
