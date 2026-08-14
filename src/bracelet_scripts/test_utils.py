import os
import subprocess
from dataclasses import dataclass
from pathlib import Path


class StaleBuildError(RuntimeError):
    pass


@dataclass(frozen=True)
class Build:
    bracelet_edges: Path
    bracelet_trace_simplify: Path
    bracelet_cc: Path
    bracelet_cxx: Path

    @classmethod
    def from_bin_dir(cls, p: Path) -> "Build":
        return cls(
            bracelet_edges=p / "bracelet-edges",
            bracelet_trace_simplify=p / "bracelet-trace-simplify",
            bracelet_cc=p / "bracelet-cc.sh",
            bracelet_cxx=p / "bracelet-c++.sh",
        )

    @classmethod
    def from_local_ninja(cls) -> "Build":
        root = Path(__file__).resolve().parent.parent.parent
        bin_dir = root / "build"
        # We don't actually do a rebuild here, since running builds in parallel, they'll trample on
        # each other.
        if (
            not subprocess.check_output(["ninja", "-n", "--verbose"], cwd=bin_dir)
            .decode("utf-8")
            .strip()
            .endswith("ninja: no work to do.")
        ):
            raise StaleBuildError("re-run ninja!")
        return cls.from_bin_dir(bin_dir)

    @classmethod
    def get(cls) -> "Build":
        bin_dir = os.environ.get("BRACELET_BIN_DIR")
        if not bin_dir:
            return cls.from_local_ninja()
        return cls.from_bin_dir(Path(bin_dir))


def test_assembly_building(build: Build, tmp_path: Path) -> None:
    src = tmp_path / "test.S"
    dst = tmp_path / "test.exe"
    src.write_text(
        """
        .text
        .intel_syntax noprefix
        .global main
        main:
            xor rax, rax
            ret
    """
    )
    subprocess.check_call([build.bracelet_cc, "-o", dst, src])
    subprocess.check_call([dst])
