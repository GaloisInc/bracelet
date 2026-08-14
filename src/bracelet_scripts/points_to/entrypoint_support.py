import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

import cxxfilt  # type: ignore[import-untyped]
import networkx as nx

from .load import DebugTable


@dataclass(init=False)
class SVFPaths:
    svf_dir: Path
    clang_dir: Path
    llvm_dir: Path

    def __init__(
        self, svf_dir: Path, clang_dir: Path | None, llvm_dir: Path | None
    ) -> None:
        self.svf_dir = svf_dir
        self.clang_dir = (
            clang_dir if clang_dir is not None else svf_dir / "llvm-16.0.0.obj"
        )
        self.llvm_dir = (
            llvm_dir if llvm_dir is not None else svf_dir / "llvm-16.0.0.obj"
        )


def process_missing(input_dir: Path, output_file: Path) -> None:
    with open(input_dir / "missing.txt", "r") as i, open(output_file, "w") as o:
        for line in i:
            line = line.strip()
            if line == "":
                continue
            entry = line.split("\t")
            try:
                demangled = cxxfilt.demangle(entry[1])
            except cxxfilt.InvalidName:
                demangled = "<invalid>"
            o.write(f"{entry[0]}\t{entry[1]}\t{demangled}\n")


def svf_run_on_path(
    bracelet_points_to: Path,
    svf_paths: SVFPaths,
    conservative: bool,
    snapshot: str,
    target_pid: int,
    wdir: Path,
    output: Path,
    save_missing: Path | None,
    save_pts: Path | None,
) -> None:
    core_file = Path(snapshot) / f"{target_pid}.core"
    exe_file = Path(snapshot) / f"{target_pid}.exe"
    trace_dir = Path(snapshot) / "bracelet-trace"
    if not trace_dir.exists() or [x for x in trace_dir.iterdir()] == []:
        trace_arg: list[str | Path] = []
    else:
        trace_arg = [f"--trace-dir={trace_dir.absolute()}"]

    svf_dir = wdir / "svf"
    svf_dir.mkdir(exist_ok=True)

    with (
        open(svf_dir / "stdout.log", "w") as stdout,
        open(svf_dir / "stderr.log", "w") as stderr,
    ):
        subprocess.run(
            [bracelet_points_to]
            + (["--conservative"] if conservative else [])
            + (["--save-pts"] if save_pts else [])
            + [
                f"--tmp={svf_dir}",
                exe_file,
                f"--sysroot={Path(snapshot).absolute()}",
                f"--core={core_file.absolute()}",
                f"--svf-dir={svf_paths.svf_dir}",
                f"--clang-dir={svf_paths.clang_dir}",
                f"--llvm-dir={svf_paths.llvm_dir}",
            ]
            + trace_arg,
            stdout=stdout,
            stderr=stderr,
            check=True,
        )

    output.mkdir(exist_ok=True)
    shutil.move(svf_dir / "cg.csv", output)
    if save_missing:
        process_missing(svf_dir, save_missing)
    if save_pts:
        shutil.move(svf_dir / "pts.csv", save_pts)

    cg: nx.DiGraph[int | str] = nx.DiGraph()
    with open(output / "cg.csv", "r") as f:
        for line in f:
            line = line.strip()
            if line == "":
                continue
            edge = line.split("\t")
            try:
                src: int | str = int(edge[0][:18], 16)
            except ValueError:
                src = edge[0]
            try:
                dst: int | str = int(edge[1], 16)
            except ValueError:
                dst = edge[1]
            cg.add_edge(src, dst)

    with (output / "DebugTable.facts").open("a") as combined:
        for table in wdir.glob("0x*/DebugTable.facts"):
            with open(table) as infile:
                for line in infile:
                    combined.write(line)

    with (output / "FastNodeInstructions.facts").open("a") as combined:
        for table in wdir.glob("0x*/FastNodeInstructions.facts"):
            with open(table) as infile:
                for line in infile:
                    combined.write(line)

    with (output / "NodeInstructions.facts").open("a") as combined:
        for table in wdir.glob("0x*/NodeInstructions.facts"):
            with open(table) as infile:
                for line in infile:
                    combined.write(line)

    with (output / "EnclosingFunctionForLocal.facts").open("a") as combined:
        for table in wdir.glob("0x*/EnclosingFunctionForLocal.facts"):
            with open(table) as infile:
                for line in infile:
                    combined.write(line)

    with (output / "FunctionNames.facts").open("a") as combined:
        for table in wdir.glob("0x*/function-name.txt"):
            with open(table) as infile:
                for line in infile:
                    combined.write(f"{table.parts[-2]}\t{line.strip()}\n")

    debug_table = DebugTable.read(output / "FunctionNames.facts")

    with (output / "reachable.csv").open("w") as f:
        mains = [n for n in debug_table["main"] if not n.is_local]
        assert len(mains) == 1
        main = mains[0]
        for reachable in nx.descendants(cg, main.symbol):
            if isinstance(reachable, int):
                f.write(f"{hex(reachable)}\n")
            else:
                f.write(f"{reachable}\n")
