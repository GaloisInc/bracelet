import argparse
import os
import shutil
import subprocess
from pathlib import Path


def gdb_snapshot(pid: int, snap_dst: Path, dead_pids: set[int]) -> None:
    proc_dir = Path(f"/proc/{pid}")
    try:
        exe = (proc_dir / "exe").resolve()
    except OSError as e:
        # NOTE: this is racy, because the process could become a zombie _after_ we check
        print(f"Skipping {pid}. Likely a zombie: {e}")
        dead_pids.add(pid)
        return
    if not exe.exists():
        state = next(
            (
                line.split()[1]
                for line in (proc_dir / "status").read_text().splitlines()
                if line.startswith("State:")
            ),
            None,
        )
        if state == "Z":
            print(f"Skipping {pid}. Process is a zombie")
            dead_pids.add(pid)
            return
        raise FileNotFoundError(
            f"{exe!r} does not exist for running process {pid}. "
            "The process may be running through an unsupported emulator."
        )
    libraries_log = snap_dst / f"{pid}.libraries.txt"
    core_file = str(snap_dst / f"{pid}.core")
    exe_cwd = Path(f"/proc/{pid}/cwd").resolve()
    subprocess.check_call(
        [
            # Based on gcore
            "gdb",
            "--nx",
            "--batch",
            "-iex",
            "set debuginfod enabled off",
            "-ex",
            "set pagination off",
            "-ex",
            "set height 0",
            "-ex",
            "set width 0",
            "-ex",
            f"attach {pid}",
            "-ex",
            f"gcore {core_file}",
            "-ex",
            f"pipe info sharedlibrary | tee {libraries_log}",
            "-ex",
            "detach",
            "-ex",
            "quit",
        ],
        stdin=subprocess.DEVNULL,
        cwd=exe_cwd,
    )
    (snap_dst / f"{pid}.exe").symlink_to(f"./{exe}")
    library_paths = [str(exe)]
    for line in libraries_log.read_text().split("\n")[1:]:
        parts = line.split()
        if len(parts) == 0:
            continue
        library_paths.append(parts[-1])
    for path in library_paths:
        dsts = {Path(f"{snap_dst}/{path}")}
        src = None
        if path.startswith("/"):
            src = Path(path)
        elif path.startswith("./"):
            for potential_src in [exe_cwd / path, exe.parent / path]:
                if potential_src.exists():
                    src = potential_src
                    dsts.add(Path(f"{snap_dst}/{potential_src.resolve()}"))
                    break
            else:
                print(f"Failed to locate library {path!r}")
        if src is not None:
            for dst in dsts:
                if dst.exists():
                    continue
                dst.parent.mkdir(parents=True, exist_ok=True)
                try:
                    shutil.copy2(src, dst)
                    print(f"Copied {src} to {dst}")
                except OSError as e:
                    print(f"Failed to copy {src} to {dst} due to {e}")


def run_and_snapshot(rem: list[str], out: Path) -> None:
    os.mkdir(out)
    curr_env = os.environ.copy()
    curr_env["BRACELET_SNAPSHOT"] = "1"

    proc = subprocess.Popen(rem, stdout=subprocess.PIPE, env=curr_env)
    try:
        # read until we close stdout
        print("about to comm")
        while True:
            # proc should error if we dont open
            if len(proc.stdout.read()) == 0:  # type: ignore
                break
        print("done")

        dead_pids: set[int] = set()
        with open(f"/proc/{proc.pid}/coredump_filter", "w") as f:
            f.write("0xfff\n")
        gdb_snapshot(proc.pid, out, dead_pids)
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("outdir", type=Path)
    parser.add_argument(
        "remaining_args", nargs=argparse.REMAINDER, help="Arguments passed after --"
    )
    args = parser.parse_args()
    run_and_snapshot(args.remaining_args, args.outdir)
