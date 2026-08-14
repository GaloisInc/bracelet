#!/usr/bin/env python3
import os
import subprocess
from collections.abc import Iterator
from pathlib import Path

from bracelet_scripts.test_utils import Build


class DlsymFactsNotFoundError(RuntimeError):
    pass


# TODO: use common edge loading code here
def read_tsv(p: Path | str) -> Iterator[tuple[str, ...]]:
    with open(p) as f:
        for line in f:
            yield tuple(line.strip().split("\t"))


def test_dlsym(
    build: Build,
    tmp_path: Path,
) -> None:
    num_threads = 128
    num_functions = 1 << 15
    tmp = tmp_path
    lib_c = tmp / "lib.c"
    lib_so = tmp / "lib.so"
    a_out = tmp / "a.out"
    lib_c.write_text(
        "#include <stdio.h>\n"
        + "\n".join(
            f'void f_{i}(void) {{ puts("f_{i}"); }}' for i in range(num_functions)
        )
    )
    root = Path(__file__).parent
    subprocess.check_call(
        [
            build.bracelet_cc,
            "-L/opt/bracelet-llvm-runtime/bin",
            "-o",
            lib_so,
            "-shared",
            lib_c,
        ]
    )
    subprocess.check_call(
        [
            build.bracelet_cc,
            "-L/opt/bracelet-llvm-runtime/bin",
            "-o",
            a_out,
            "-ldl",
            "-pthread",
            f"-DNUM_FUNCTIONS={num_functions}",
            f"-DNUM_THREADS={num_threads}",
            root / "user.c",
            root / "user2.c",
        ]
    )
    subprocess.check_call(
        [a_out],
        cwd=tmp,
        env=os.environ | {"LD_LIBRARY_PATH": "/opt/bracelet-llvm-runtime/bin"},
    )
    core = next(iter(tmp.glob("core*")))
    edges = tmp / "edges"
    edges.mkdir()
    subprocess.check_call(
        [build.bracelet_edges, a_out, "--sysroot", "/", "--core", core], cwd=edges
    )
    functions = list(edges.glob("0x*"))
    for f in functions:
        if (f / "function-name.txt").read_text() != "background":
            continue
        dlsym_facts = list(read_tsv(f / "Dlsym.facts"))
        if len(dlsym_facts) > 0:
            break
    else:
        raise DlsymFactsNotFoundError
    assert len(dlsym_facts) == 1
    callsite = dlsym_facts[0][0]
    dlsym_outputs = []
    for dst, src in read_tsv(f / "Assign.facts"):
        if dst == callsite:
            dlsym_outputs.append(src)
            assert ":" not in src
    dlsym_functions = {
        (edges / out / "function-name.txt").read_text().strip() for out in dlsym_outputs
    }
    expected = {f"f_{i}" for i in range(num_functions)}
    print("UNEXPECTED: " + repr(dlsym_functions - expected))
    print(
        f"MISSING[{len(expected - dlsym_functions)}]: "
        + repr(expected - dlsym_functions)
    )
    assert expected == dlsym_functions
    for data in read_tsv(f / "DebugTable.facts"):
        assert len(data) == 2
