import os
import random
import subprocess
import tempfile
from collections.abc import Iterable
from pathlib import Path

import click
import networkx as nx
import pytest

from bracelet_scripts.points_to.emit_c import EmitC, addr_for_name, name_for_addr
from bracelet_scripts.points_to.load import Edges, Node, Symbol
from bracelet_scripts.points_to.svf import Svf
from bracelet_scripts.test_utils import Build
from bracelet_scripts.tracing import read_trace_sites, read_traces


def test_name_addr_conversion() -> None:
    rng = random.Random(b"A lovely little seed. Verily!")
    for _ in range(128):
        for bits in range(64):
            addr = rng.randrange(0, 1 << bits)
            assert addr_for_name(name_for_addr(addr)) == addr
            assert addr_for_name(name_for_addr(addr) + "_") is None


@pytest.fixture(scope="session")
def svf() -> Svf:
    return Svf()


def check_points_to(
    trace_files: Iterable[Path] | None,
    svf: Svf,
    build: Build,
    tmp_path: Path,
    exe: Path,
    core: Path,
    sysroot: Path,
) -> tuple[EmitC, "nx.DiGraph[str]"]:
    """
    Return (EmitC, svf_call_graph)
    """
    edges = tmp_path / "edges"
    edges.mkdir()
    subprocess.check_call(
        [
            build.bracelet_edges,
            exe,
            "--core",
            core,
            "--sysroot",
            sysroot,
            "--call-arg-count",
        ],
        cwd=edges,
    )
    all_edges = [Edges.read(f) for f in edges.glob("0x*")]
    emit_c = EmitC(all_edges)
    cg_svf = emit_c.cg_svf(svf, tmp_path)
    nx.drawing.nx_pydot.write_dot(cg_svf, tmp_path / "cg_svf.dot")
    if trace_files is not None:
        cg_trace: nx.DiGraph[Symbol] = nx.DiGraph()
        trace_sites = read_trace_sites(edges / "trace_sites")
        for edge in read_traces(build, trace_files):
            cg_trace.add_edge(trace_sites[edge.trace_site].function.address, edge.value)
        assert len(cg_trace.edges) > 0
        nx.drawing.nx_pydot.write_dot(cg_trace, tmp_path / "cg_trace.dot")
        missing = set()
        for caller, callee in cg_trace.edges:
            caller_name = name_for_addr(caller)
            if not any(
                cg_svf.has_edge(caller_name, callee_name)
                for callee_name in emit_c.candidate_symbol_names(Node(callee, None))
            ):
                missing.add((caller, callee))
        assert len(missing) == 0, (
            repr(missing)
            + "\n"
            + repr(
                [
                    (
                        emit_c.debug_table.str(Node(caller, None)),
                        emit_c.debug_table.str(Node(callee, None)),
                    )
                    for caller, callee in missing
                ]
            )
        )
    return emit_c, cg_svf


@pytest.mark.parametrize(
    "program_name",
    [
        "one.c",
        "two.c",
        "three.c",
        "vtable.c",
        "vtable_setter.c",
        "five.cpp",
        "five_between_functions.cpp",
        "calling-undefined-function-pointers.c",
        "pthread_create.c",
        "nested_indirect_calls.cpp",
    ],
)
@pytest.mark.parametrize("opt_level", ["-O0", "-O1", "-O3"])
def test_programs(
    build: Build, tmp_path: Path, svf: Svf, program_name: str, opt_level: str
) -> None:
    do_test_program(
        build=build,
        tmp_path=tmp_path,
        svf=svf,
        program_name=program_name,
        opt_level=opt_level,
    )


def do_test_program(
    build: Build, tmp_path: Path, svf: Svf, program_name: str, opt_level: str
) -> tuple[EmitC, "nx.DiGraph[str]"]:
    program = Path(__file__).parent / "test_programs" / program_name
    exe = tmp_path / "exe"
    traces = tmp_path / "traces"
    ll = tmp_path / "ir.ll"
    traces.mkdir()
    cpp = program.suffix != ".c"
    args = [
        str(build.bracelet_cxx if cpp else build.bracelet_cc),
        str(program),
        "-pthread",
        "-L/opt/bracelet-llvm-runtime/bin",
    ]
    if opt_level:
        args.append(opt_level)
    env = os.environ | {"BRACELET_TRACE": "indirect_callees"}
    subprocess.check_call(
        args
        + [
            "-o",
            str(exe),
        ],
        env=env,
    )
    subprocess.check_call(args + ["-S", "-emit-llvm", "-o", str(ll)], env=env)
    subprocess.check_call(
        [exe],
        cwd=tmp_path,
        env=os.environ
        | {
            "BRACELET_TRACE_DIR": str(traces),
            "LD_LIBRARY_PATH": "/opt/bracelet-llvm-runtime/bin",
        },
    )
    core = next(iter(tmp_path.glob("core*")))
    return check_points_to(
        trace_files=traces.glob("trace*"),
        svf=svf,
        build=build,
        tmp_path=tmp_path,
        exe=exe,
        core=core,
        sysroot=Path("/"),
    )


@pytest.mark.parametrize("opt_level", ["-O0", "-O1", "-O3"])
def test_transparent_override(
    build: Build, tmp_path: Path, svf: Svf, opt_level: str
) -> None:
    emit_c, cg_svf = do_test_program(
        build=build,
        tmp_path=tmp_path,
        svf=svf,
        opt_level=opt_level,
        program_name="test_transparent_override.c",
    )
    assert (
        name_for_addr(next(iter(emit_c.debug_table["body"])).symbol),
        "realloc",
    ) in cg_svf.edges


@click.command()
@click.option(
    "--pid", type=int, required=True, help="The PID of the process to operate on"
)
@click.option(
    "--tmp",
    type=click.Path(path_type=Path),
    default=None,
    help="If set, use --tmp as the temporary directory and don't delete it.",
)
@click.option(
    "--trace/--no-trace",
    default=True,
    help="If set, compare the svf output to the dynamic trace.",
)
@click.argument(
    "snapshot",
    type=click.Path(path_type=Path),
    required=True,
)
def check_snapshot(pid: int, snapshot: Path, tmp: Path | None, trace: bool) -> None:
    """
    Check that the static analysis of a snapshot matches the traced snapshot
    """
    with tempfile.TemporaryDirectory() as tmp_str:
        tmp_path = tmp or Path(tmp_str)
        tmp_path = tmp_path.resolve()
        svf = Svf()
        build = Build.get()
        snapshot = snapshot.resolve()
        check_points_to(
            trace_files=(
                (snapshot / "bracelet-trace").glob(f"trace-{pid}_*") if trace else None
            ),
            svf=svf,
            build=build,
            tmp_path=tmp_path,
            exe=snapshot / f"{pid}.exe",
            core=snapshot / f"{pid}.core",
            sysroot=snapshot,
        )


if __name__ == "__main__":
    check_snapshot()
