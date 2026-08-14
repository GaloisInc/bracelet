import subprocess
import sys


class Screach:

    def __init__(
        self, cg_path: str, timeout: int, solver_timeout: int, target_ecfs: str
    ) -> None:
        self.cg_path = cg_path
        self.timeout = timeout
        self.solver_timeout = solver_timeout
        self.target_ecfs = target_ecfs

    def run_screach(self, target_address: str) -> str:
        cmd = [
            "screach",
            "--callgraph",
            self.cg_path,
            "--entry-symbol",
            "main",
            "--target-addr",
            target_address,
            "--explore",
            "--timeout",
            str(self.timeout),
            "--solver-timeout",
            str(self.solver_timeout),
            "-v",
            self.target_ecfs,
        ]
        print(" ".join(cmd), file=sys.stderr)
        res = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=False,
        )

        return res.stderr
