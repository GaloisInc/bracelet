import subprocess
import tempfile
from collections.abc import Iterable, Iterator
from pathlib import Path

from bracelet_scripts.test_utils import Build
from RuntimeFormat.runtime_format import (
    BraceletTraceEdge,
    BraceletTraceSite,
)


def read_trace_sites(trace_sites: Path) -> dict[int, BraceletTraceSite]:
    out = {}
    for f in trace_sites.glob("*.bin"):
        blob = memoryview(f.read_bytes())
        size = BraceletTraceSite.type().layout.size
        count = len(blob) // size
        base = int(f.stem.replace("0x", ""), 16)
        for i in range(count):
            out[base + i * size] = BraceletTraceSite.type().unpack(
                blob[i * size : (i + 1) * size]
            )
    return out


def read_traces(
    build: Build,
    trace_data: Iterable[Path],
    simplify_traces: bool = True,
) -> Iterator[BraceletTraceEdge]:
    """
    Given the trace_sites/ directory (from bracelet-edges) and the trace data (from $BRACELET_TRACE_DIR)
    yield traces.

    The trace_data should be the paths just for the PID you care about.

    If simplify_traces is true, then loading will be much faster, but (trace_site, value) pairs will
    be deduped.
    """
    if simplify_traces:
        with tempfile.TemporaryDirectory() as tmp_str:
            tmp = Path(tmp_str)
            out = tmp / "out.bin"
            subprocess.check_call(
                [
                    str(build.bracelet_trace_simplify),
                    "-o",
                    str(out),
                ]
                + [str(x) for x in trace_data]
            )
            yield from _read_traces_inner([out])
    else:
        yield from _read_traces_inner(trace_data)


def _read_traces_inner(
    trace_data: Iterable[Path],
) -> Iterator[BraceletTraceEdge]:
    for trace_file in trace_data:
        with trace_file.open("rb") as f:
            size = BraceletTraceEdge.type().layout.size
            while True:
                blob = memoryview(f.read(size))
                if len(blob) < size:
                    break
                te = BraceletTraceEdge.type().unpack(blob)
                if te.trace_site == 0:
                    continue
                yield te
