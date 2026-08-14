#!/usr/bin/env python3
import json
import re
from pathlib import Path

import click


@click.command()
@click.option("--input", type=click.Path(path_type=Path))
@click.option("--output", type=click.Path(path_type=Path))
def gen_template(input: Path, output: Path) -> None:
    """
    Turn a C++ template into source code

    Uses erb-like <%= value %> to print a value and <% statement %> to emit a statement
    """
    out = ""

    def write_text(txt: str) -> None:
        nonlocal out
        if txt != "":
            out += f"out_stream << {json.dumps(txt)};\n"

    input_src = input.read_text().strip()
    last_pos = 0
    for m in re.finditer(r"<%(=)?(.+?)%>", input_src, re.DOTALL):
        write_text(input_src[last_pos : m.start()])
        last_pos = m.end()
        if m.group(1):
            out += f"out_stream << {m.group(2)};\n"
        else:
            out += f"{m.group(2)}\n"
    write_text(input_src[last_pos:])
    if (not output.exists()) or output.read_text() != out:
        output.write_text(out)


if __name__ == "__main__":
    gen_template()
