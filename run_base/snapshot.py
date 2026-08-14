#!/usr/bin/env python3
import os
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path
import argparse
from collections import defaultdict

def gdb_snapshot(pid: int, snap_dst: Path, dead_pids: set):
    try:
        exe = Path(f"/proc/{pid}/exe").resolve()
        if not exe.exists():
            raise FileNotFoundError(f"{repr(exe)} does not exist")
    except Exception as e:
        # NOTE: this is racy, because the process could become a zombie _after_ we check
        print(f"Skipping {pid}. Likely a zombie: {e}")
        dead_pids.add(pid)
        return
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
                print(f"Failed to locate library {repr(path)}")
        if src is not None:
            for dst in dsts:
                if dst.exists():
                    continue
                dst.parent.mkdir(parents=True, exist_ok=True)
                try:
                    shutil.copy2(src, dst)
                    print(f"Copied {src} to {dst}")
                except Exception as e:
                    print(f"Failed to copy {src} to {dst} due to {e}")

def socket_snapshot():
    print("BRACELET snapshot.py about to open socket")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server_socket:
        print("BRACELET snapshot.py about to bind")
        server_socket.bind(("", 9040))
        print("BRACELET snapshot.py about to listen")
        server_socket.listen(1)
        try:
            print("BRACELET snapshot.py about to accept")
            client_socket, _ = server_socket.accept()
        except KeyboardInterrupt:
            sys.exit(0)
        print("BRACELET snapshot.py accepted")
        start_ns = time.perf_counter_ns()
        try:
            # Now it's time to snapshot!
            pids = []
            for proc_dir in Path("/proc").iterdir():
                if proc_dir.is_dir() and proc_dir.name.isdigit():
                    pid = int(proc_dir.name)
                    if pid != os.getpid():
                        pids.append(pid)
            pids.sort()

            SNAPSHOT_DIR = Path(os.environ["SNAPSHOT_DIR"])
            snap_dst = SNAPSHOT_DIR / "snapshot"
            snap_dst.mkdir(exist_ok=True, parents=True)
            dead_pids = set()
            for i, pid in enumerate(pids):
                print(f"Dumping pid {pid} [{i + 1}/{len(pids)}]")
                gdb_snapshot(pid,snap_dst, dead_pids)
            for i, pid in enumerate(pids):
                print(f"ECFS Snapshotting pid {pid} [{i + 1} / len(pids)]")
                if pid in dead_pids:
                    print(f"{pid} was reported dead. skipping")
                    continue
                with (SNAPSHOT_DIR / f"ecfs.{pid}.stderr.txt").open("wb") as err:
                    try:
                        subprocess.check_call(
                        [
                            "/opt/ecfs/bin/ecfs",
                            "-S",
                            str(pid),
                            "-o",
                            str(snap_dst / f"{pid}.ecfs.core"),
                        ],
                        stderr=err,
                        )
                    except subprocess.CalledProcessError as e:
                        print(f"ecfs failed with exit code: {e.returncode}")
            shutil.copytree("/bracelet-trace", snap_dst / "bracelet-trace")
            print(
                f"Finished snapshotting in {time.perf_counter_ns() - start_ns}ns, now compressing"
            )
            start_ns = time.perf_counter_ns()
            subprocess.check_call(
                [
                    "bash",
                    "-c",
                    "set -euxo pipefail; tar -c ./snapshot | zstd -T0 -9 > snapshot.tar.zst",
                ],
                cwd=SNAPSHOT_DIR,
            )
            shutil.rmtree(snap_dst)
            print(f"Finished compressing in {time.perf_counter_ns() - start_ns}ns")
            client_socket.sendall(b"snapshot-complete\n")
        finally:
            client_socket.close()

if __name__ == "__main__":
    prsr = argparse.ArgumentParser()
    prsr.add_argument("--pid", default=None)
    prsr.add_argument("--snap-dir",default=None, type=Path)
    args = prsr.parse_args()

    if args.pid is not None:
        with open(f"/proc/{args.pid}/coredump_filter", "w") as f:
            f.write("0xfff\n")
        gdb_snapshot([args.pid], args.snap_dir, set())
    else:
        socket_snapshot()
