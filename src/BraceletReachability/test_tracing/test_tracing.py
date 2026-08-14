#!/usr/bin/env python3
import contextlib
import os
import subprocess
from collections import defaultdict
from collections.abc import Iterator
from pathlib import Path

from bracelet_scripts.points_to.load import Symbol
from bracelet_scripts.test_utils import Build
from bracelet_scripts.tracing import read_trace_sites, read_traces


class TracingTestError(RuntimeError):
    pass


def gen_code(num_functions: int, num_iterations: int) -> str:
    out = "\n"
    indent_level = 0

    def line(x: str) -> None:
        nonlocal out
        nonlocal indent_level
        out += "    " * indent_level
        out += f"{x}\n"

    @contextlib.contextmanager
    def indent() -> Iterator[None]:
        nonlocal indent_level
        indent_level += 1
        yield
        indent_level -= 1

    # f0() doesn't call anything, so it doesn't count in num_functions
    for i in range(num_functions + 1):
        line(f"void f{i}(void);")
        line(f"void g{i}(void);")
    for i in range(num_functions + 1):
        line(f"void __attribute__((noinline)) f{i}(void) {{")
        with indent():
            if i != 0:
                line(f"(blackBox(f{i - 1}))();")
        line("}")
    for i in range(num_functions):
        line(f"void g{i}(void) {{")
        with indent():
            # Prevent LLVM from merging these functions
            line('__asm__ volatile("");')
        line("}")
    line(
        "func_t GFunctions[] = {"
        + ", ".join(f"g{i}" for i in range(num_functions))
        + ", NULL};"
    )
    line(
        f"void testCode() {{ for(int i=0; i < {num_iterations}; i++) "
        f"f{num_functions}(); }}"
    )
    line("void printFunctions() {")
    with indent():
        symbols = (
            [f"f{i}" for i in range(num_functions + 1)]
            + [f"g{i}" for i in range(num_functions)]
            + [
                "braceletTraceWriteAll",
                "listFds",
                "listMaps",
                "backgroundThread",
                "testMegamorphic",
            ]
        )
        for x in symbols:
            line(f'printf("==== {x}\\n%p\\n---- {x}\\n", {x});')
    line("}")
    return out


def test_tracing(
    build: Build,
    tmp_path: Path,
) -> None:
    num_threads = 128
    num_functions = 128
    num_iterations = 2048
    simplify_traces = True
    tmp = tmp_path
    main_c = tmp / "main.c"
    a_out = tmp / "a.out"
    trace_dir = tmp / "trace_dir"
    trace_dir.mkdir(exist_ok=True)
    root = Path(__file__).parent
    edges = tmp / "edges"
    testMegamorphic_c = root / "testMegamorphic.c"
    testMegamorphic_so = tmp / "testMegamorphic.so"
    main_c.write_text(
        (root / "main.c").read_text() + gen_code(num_functions, num_iterations)
    )
    flags = [
        "-pthread",
        f"-DNUM_FUNCTIONS={num_functions}",
        f"-DNUM_THREADS={num_threads}",
        f"-DNUM_ITERATIONS={num_iterations}",
        "-fPIC",
        f"-I{root}",
    ]
    env = os.environ | {"BRACELET_TRACE": "indirect_callees"}
    subprocess.check_call(
        [
            str(build.bracelet_cc),
            "-L/opt/bracelet-llvm-runtime/bin",
            "-o",
            str(tmp / "testMegamorphic.ll"),
            "-S",
            "-emit-llvm",
            str(testMegamorphic_c),
        ]
        + flags,
        env=env,
    )
    subprocess.check_call(
        [
            str(build.bracelet_cc),
            "-L/opt/bracelet-llvm-runtime/bin",
            "-o",
            str(tmp / "testMegamorphic.asm"),
            "-S",
            str(testMegamorphic_c),
        ]
        + flags,
        env=env,
    )
    subprocess.check_call(
        [
            str(build.bracelet_cc),
            "-L/opt/bracelet-llvm-runtime/bin",
            "-shared",
            "-o",
            str(testMegamorphic_so),
            str(testMegamorphic_c),
        ]
        + flags,
        env=env,
    )
    subprocess.check_call(
        [
            str(build.bracelet_cc),
            "-L/opt/bracelet-llvm-runtime/bin",
            "-o",
            str(a_out),
            str(testMegamorphic_so),
            str(main_c),
        ]
        + flags,
        env=env,
    )
    subprocess.check_call(
        [
            str(build.bracelet_cc),
            "-L/opt/bracelet-llvm-runtime/bin",
            "-o",
            str(tmp / "main.ll"),
            "-S",
            "-emit-llvm",
            str(main_c),
        ]
        + flags,
        env=env,
    )
    stdout = subprocess.check_output(
        [a_out],
        cwd=tmp,
        env=os.environ
        | {
            "BRACELET_TRACE_DIR": str(trace_dir),
            "LD_LIBRARY_PATH": "/opt/bracelet-llvm-runtime/bin",
        },
    ).decode("ascii")
    core = next(iter(tmp.glob("core*")))
    edges.mkdir()
    subprocess.check_call(
        [
            build.bracelet_edges,
            a_out,
            "--core",
            core,
            "--sysroot",
            "/",
            "--call-arg-count",
        ],
        cwd=edges,
    )
    data = {}
    for item in stdout.split("==== ")[1:]:
        nl = item.index("\n")
        key = item[0:nl]
        body = item[nl:].split("----")[0]
        data[key] = body.strip()
    assert {
        line.split()[0] for line in data["/proc/self/fd"].split("\n") if line != ""
    } == {"0", "1", "2"}, data["/proc/self/fd"]
    # The only mapped files from tmp should be a.out and our .so
    assert str(tmp) not in "\n".join(
        line
        for line in data["/proc/self/maps"].split("\n")
        if str(a_out) not in line and str(testMegamorphic_so) not in line
    ), data["/proc/self/maps"]
    addr2function = {
        int(v.strip(), 16): k for k, v in data.items() if len(v.split("\n")) == 1
    }
    # counts[caller][callee]
    counts: defaultdict[Symbol, defaultdict[Symbol, int]] = defaultdict(
        lambda: defaultdict(lambda: 0)
    )
    trace_sites = read_trace_sites(edges / "trace_sites")
    for edge in read_traces(
        build,
        trace_dir.glob("trace-*"),
        simplify_traces=simplify_traces,
    ):
        callee = edge.value
        counts[trace_sites[edge.trace_site].function.address][callee] += 1
    named_counts = {
        addr2function[caller]: {
            addr2function[callee]: count for callee, count in callees.items()
        }
        for caller, callees in counts.items()
    }
    expected = {
        "testMegamorphic": {
            f"g{i}": 1 if simplify_traces else num_threads for i in range(num_functions)
        }
    } | {
        # Iterations don't matter here because they should all be compressed
        f"f{i+1}": {f"f{i}": 1 if simplify_traces else num_threads}
        for i in range(num_functions)
    }
    if named_counts != expected:
        assert named_counts.keys() == expected.keys(), repr(
            (
                set(named_counts.keys()) - set(expected.keys()),
                set(expected.keys()) - set(named_counts.keys()),
            )
        )
        for k, v_e in expected.items():
            if named_counts[k] != v_e:
                print(k)
                print(sorted(v_e.items()))
                print(sorted(named_counts[k].items()))
        raise TracingTestError
