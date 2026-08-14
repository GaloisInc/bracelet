import subprocess
import sys
from os import PathLike
from pathlib import Path

from elftools.elf.elffile import ELFFile


def has_bracelet_graph_section(target_path: Path) -> bool:
    if target_path.is_file():
        with open(target_path, "rb") as f:
            elf = ELFFile(f)  # type: ignore[no-untyped-call]
            return elf.has_section("GR_graph_edges") or elf.has_section(  # type: ignore[no-any-return,no-untyped-call]
                "__DATA,__GR_graph_edges"
            )
    else:
        print(
            f"Warning: {target_path} is invalid (missed target binary?)",
            file=sys.stderr,
        )
        return False


def select_pid(dir_target: Path, pid: int | None, target_bin: str | None = None) -> int:
    if pid is not None:
        return pid

    pids = []
    for x in dir_target.glob("*.exe"):
        if has_bracelet_graph_section(x):
            is_correct_bin = target_bin is None or x.resolve().name == target_bin
            if is_correct_bin:
                pids.append(int(x.stem))

    return min(pids)


def run_bracelet_edges(
    snapshot: str | PathLike[str],
    pid: int | None,
    wdir: str | PathLike[str],
    bracelet_edges: str | PathLike[str],
    target_bin: str | None,
    add_call_arg_count: bool,
) -> None:
    target_pid = select_pid(Path(snapshot), pid, target_bin=target_bin)
    print(f"Selected target_pid {target_pid}", file=sys.stderr)
    core_file = Path(snapshot) / f"{target_pid}.core"
    exe_file = Path(snapshot) / f"{target_pid}.exe"

    subprocess.run(
        [
            bracelet_edges,
            f"--sysroot={Path(snapshot).absolute()}",
            f"--core={core_file.absolute()}",
            exe_file.absolute(),
        ]
        + (["--call-arg-count"] if add_call_arg_count else []),
        check=True,
        cwd=wdir,
    )
