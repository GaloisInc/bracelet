import re
import sys
from collections import defaultdict
from collections.abc import Iterator
from pathlib import Path

import jinja2
import networkx as nx

from .load import DebugTable, Edges, Node
from .svf import Svf

_template_env = jinja2.Environment(
    loader=jinja2.PackageLoader("bracelet_scripts.points_to"),
    autoescape=False,
    trim_blocks=True,
    lstrip_blocks=True,
)
_template = _template_env.get_template("c.tmpl")
_prelude = _template_env.get_template("prelude.h")
_declare_overrides = _template_env.get_template("declare_overrides.tmpl")
_define_globals = _template_env.get_template("define_globals.tmpl")

NODE_PREFIX = "BrAcElEtNoDe"
NODE_SUFFIX = "X"


def name_for_addr(addr: int) -> str:
    return f"{NODE_PREFIX}{addr:x}{NODE_SUFFIX}"


def addr_for_name(name: str) -> int | None:
    if name.startswith(NODE_PREFIX) and name.endswith(NODE_SUFFIX):
        return int(name[len(NODE_PREFIX) : -len(NODE_SUFFIX)], 16)
    else:
        return None


_RE_NOT_WORD = re.compile(r"[\W]")
_RE_MULTIPLE_UNDERSCORE = re.compile(r"[_]+")


class EmitC:
    def __init__(self, all_edges: list[Edges]) -> None:
        self.all_edges = all_edges
        # TODO: we currently hold all the edges in memory, but we don't _need_ to do that
        self.prelude = _prelude.render()
        # For a given C symbol name x, override_nargs[x] is nargs for x()
        self.override_nargs: dict[str, int] = {}
        for m in re.finditer(
            r"DEF_OVERRIDE(_INLINE)?\((?P<name>[\w]+)\)[\s]*\((?P<args>[\s,\w]+)\)",
            self.prelude,
        ):
            arg_str = m.group("args").strip()
            if arg_str in ("", "void"):
                nargs = 0
            else:
                nargs = len(arg_str.split(","))
            self.override_nargs[m.group("name")] = nargs
        # Map a Node in the address space of the traced process to the name of an override
        self.overrides: dict[Node, str] = {}
        for edges in all_edges:
            symbol_short_names = {
                k.split("@")[0]: v for k, v in edges.debug_table.reverse.items()
            }
            for o in self.override_nargs:
                if o in symbol_short_names:
                    nodes = symbol_short_names[o]
                    for n in nodes:
                        if not n.is_local:
                            self.overrides[n] = o
        # maps symbol to number of arguments
        self.nargs: dict[Node, int] = {
            n: self.override_nargs[sym] for n, sym in self.overrides.items()
        }
        for edges in all_edges:
            defined_args = 0
            for ad in edges.argument_definition:
                assert ad.func == edges.function
                defined_args = max(ad.arg_no + 1, defined_args)
            if edges.function in self.nargs:
                assert self.nargs[edges.function] == defined_args, repr(
                    (
                        defined_args,
                        self.nargs[edges.function],
                        edges.function,
                        edges.debug_table[edges.function],
                    )
                )
            else:
                self.nargs[edges.function] = defined_args
            for call in edges.call:
                if call.callee.is_local:
                    continue
                if call.callee in self.nargs:
                    assert call.nargs == self.nargs[call.callee], repr(
                        (
                            call.nargs,
                            self.nargs[call.callee],
                            call.callee,
                            edges.debug_table[call.callee],
                        )
                    )
                else:
                    self.nargs[call.callee] = call.nargs
        undefined_functions = (
            set(self.nargs.keys())
            - {e.function for e in all_edges}
            - set(self.overrides.keys())
        )
        # This is a debug table that combines the debug tables of all functions
        self.debug_table = DebugTable()
        for edges in self.all_edges:
            self.debug_table.add_from(edges.debug_table)
        if len(undefined_functions) > 0:
            # TODO: this isn't a complete list. We might still be missing functions which are only
            # ever referenced via function pointer.
            print("UNDEFINED FUNCTIONS:", file=sys.stderr)
            for uf in undefined_functions:
                print(f" - {self.debug_table.str(uf)}", file=sys.stderr)
            print(file=sys.stderr)

    def candidate_symbol_names(self, n: Node) -> Iterator[str]:
        """
        What names in the C source might be used to refer to `n`?
        """
        assert not n.is_local
        yield name_for_addr(n.symbol)
        if n in self.overrides:
            ovr = self.overrides[n]
            yield "bracelet_override_" + ovr
            yield ovr

    def _local_short_names(
        self, locals: set[Node], debug_table: DebugTable
    ) -> dict[Node, str]:
        """
        Give short identifiers to locals.

        Locals without names in the dict will be given generic names.
        """
        local2name: dict[Node, str] = {}
        name2local: dict[str, Node] = {}
        for node in sorted(locals):
            debug_names = debug_table[node]
            if len(debug_names) > 1:
                continue
            debug_name = next(iter(debug_names))
            name = "LOCAL" + _RE_MULTIPLE_UNDERSCORE.sub(
                "_", _RE_NOT_WORD.sub("_", debug_name.split("|")[-1])
            )
            while name in name2local:
                # Doing _1_1_1 isn't great, but I don't think we actually have much ambiguity
                name += "_1"
            name2local[name] = node
            local2name[node] = name
        return local2name

    def _function(self, edges: Edges, all_globals: set[Node]) -> Iterator[str]:
        globals: set[Node] = set()
        locals: set[Node] = set()
        local_short_names: dict[Node, str] = {}

        def ident(n: Node) -> str:
            if n.is_local:
                return local_short_names.get(
                    n,
                    f"n{hex(n.symbol)}_{n.local_idx}",
                )
            else:
                out = name_for_addr(n.symbol)
                if n in edges.debug_table:
                    out += "/*"
                    out += ", ".join(edges.debug_table[n])
                    out += "*/"
                return out

        def nondet_select(values: list[Node]) -> str:
            if len(values) == 0:
                return "NULL"
            else:
                out = ident(values[0])
                for x in values[1:]:
                    out = f"NONDET_SELECT({out}, {ident(x)})"
                return out

        for edge in edges.all_edges():
            for n in edge.nodes:
                if n.is_local:
                    assert n.symbol == edges.function.symbol
                    locals.add(n)
                if n not in self.nargs and not n.is_local:
                    # TODO: this is a candidate for an undefined function
                    globals.add(n)
        all_globals |= globals
        local_short_names = self._local_short_names(locals, edges.debug_table)

        # For each callsite, map arguments to nodes
        argument_supply: defaultdict[Node, defaultdict[int, list[Node]]] = defaultdict(
            lambda: defaultdict(list)
        )
        for e in edges.argument_supply:
            argument_supply[e.callsite][e.arg_no].append(e.value)
        return _template.generate(
            ident=ident,
            edges=edges,
            nargs=self.nargs,
            overrides=self.overrides,
            globals=globals,
            locals=locals,
            argument_supply=argument_supply,
            nondet_select=nondet_select,
            debug_table=edges.debug_table,
        )

    def output(self, dst: Path) -> None:
        """
        Write C code for edges to `dst`
        """
        prelude = "\n".join(
            [
                self.prelude,
                _declare_overrides.render(
                    overrides={
                        name_for_addr(n.symbol): symbol
                        for n, symbol in self.overrides.items()
                    },
                ),
            ]
        )
        (dst / "prelude.h").write_text(prelude)
        all_globals: set[Node] = set()
        for edges in self.all_edges:
            with (dst / f"{hex(edges.function.symbol)}.c").open("w") as f:
                for chunk in self._function(edges, all_globals):
                    f.write(chunk)

        def global_ident(n: Node) -> str:
            assert not n.is_local
            return name_for_addr(n.symbol)

        (dst / "globals.c").write_text(
            _define_globals.render(
                globals=all_globals,
                ident=global_ident,
            )
        )

    def cg_svf(self, svf: Svf, work_dir: Path) -> "nx.DiGraph[str]":
        c = work_dir / "c"
        c.mkdir()
        self.output(c)
        bitcode = svf.compile_bitcode(c)
        return svf.construct_callgraph(bitcode)
