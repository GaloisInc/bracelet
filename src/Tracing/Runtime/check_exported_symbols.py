#!/usr/bin/env python3
import subprocess
from pathlib import Path

import click

# nm --defined-only -D build/libtrace_runtime.so


@click.command()
@click.argument("libtrace_runtime_so", required=True, type=click.Path(path_type=Path))
def check_exported_symbols(libtrace_runtime_so: Path) -> None:
    """
    Check that <libtrace_runtime_so> exports only the right symbols
    """
    # TODO: run ldd and make sure that it only links to system libraries.
    found_symbols = {
        parts[-1].split("@")[0]
        for parts in (
            line.split()
            for line in subprocess.check_output(["nm", "-D", libtrace_runtime_so])
            .decode("utf-8")
            .strip()
            .split("\n")
        )
        if len(parts) == 3
    }
    print(sorted(found_symbols))
    expected_symbols = {
        "aligned_alloc",
        "free",
        "realloc",
        "_ZdlPv",
        "cfree",
        "_ZdlPvm",
        "_Znam",
        "__libc_malloc",
        "_ZnamRKSt9nothrow_t",
        "malloc_usable_size",
        "_ZnwmRKSt9nothrow_t",
        "_ZdaPvm",
        "__libc_memalign",
        "memalign",
        "__libc_free",
        "__libc_realloc",
        "reallocarray",
        "posix_memalign",
        "calloc",
        "_Znwm",
        "malloc",
        "_ZdaPv",
        "__libc_calloc",
        "dlclose",
        "dlopen",
        "braceletTraceBuffer",
        "braceletTraceWord",
        "braceletTraceTagAllocation",
        "braceletTraceAllocaFree",
        "braceletTraceAllocaAllocate",
        # Exported for tests
        "_ZN8vhmalloc11PointerInfo2ofEm",
        "_ZN8vhmalloc6setTagEPvm",
        "_ZN14bracelet_trace12elf_segments21pointerIsInElfSegmentEm",
    }
    assert expected_symbols == found_symbols - {"LIB_BRACELET_TRACING_RUNTIME"}, repr(
        found_symbols - expected_symbols
    )


if __name__ == "__main__":
    check_exported_symbols()
