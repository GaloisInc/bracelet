import builtins
import dataclasses
import gzip
import itertools
from abc import ABC, abstractmethod
from collections.abc import Iterator
from dataclasses import dataclass, field
from pathlib import Path
from typing import Self, overload


def read_tsv(p: str | Path) -> Iterator[tuple[str, ...]]:
    with open(p, "rb") as f:
        is_gzip = f.read(2) == b"\x1f\x8b"
    with gzip.open(p, "rt") if is_gzip else open(p, "r") as f:
        for line in f:
            line = line.strip()
            if line == "":
                continue
            yield tuple(line.split("\t"))


type Symbol = int
type LocalIdx = int


@dataclass(frozen=True, order=True)
class Node:
    symbol: Symbol
    local_idx: LocalIdx | None

    @property
    def is_local(self) -> bool:
        return self.local_idx is not None

    @classmethod
    def parse(cls, x: str) -> "Node":
        parts = x.split(":")
        if len(parts) not in (1, 2):
            raise ValueError(f"Cannot parse: {x!r}")
        return cls(
            symbol=int(parts[0], 16),
            local_idx=int(parts[1], 16) if len(parts) == 2 else None,
        )


@dataclass(frozen=True)
class DebugTable:
    forward: dict[Node, set[str]] = field(default_factory=dict)
    reverse: dict[str, set[Node]] = field(default_factory=dict)

    @classmethod
    def read(cls, p: Path) -> "DebugTable":
        forward: dict[Node, set[str]] = {}
        reverse: dict[str, set[Node]] = {}
        for node_str, pretty in read_tsv(p):
            node = Node.parse(node_str)
            if node not in forward:
                forward[node] = set()
            if pretty not in reverse:
                reverse[pretty] = set()
            forward[node].add(pretty)
            reverse[pretty].add(node)
        return DebugTable(forward, reverse)

    def add_from(self, debug_table: "DebugTable") -> None:
        for k, v in debug_table.forward.items():
            if k not in self.forward:
                self.forward[k] = set()
            self.forward[k].update(v)
        for k2, v2 in debug_table.reverse.items():
            if k2 not in self.reverse:
                self.reverse[k2] = set()
            self.reverse[k2].update(v2)

    def __contains__(self, k: str | Node) -> bool:
        return k in self.forward or k in self.reverse

    def str(self, k: Node) -> str:
        if k in self:
            return repr(self[k])
        else:
            return repr(k)

    @overload
    def __getitem__(self, k: builtins.str) -> set[Node]: ...
    @overload
    def __getitem__(self, k: Node) -> set[builtins.str]: ...
    def __getitem__(self, k: builtins.str | Node) -> set[builtins.str] | set[Node]:
        if isinstance(k, str):
            return self.reverse[k]
        elif isinstance(k, Node):
            return self.forward[k]
        else:
            raise TypeError(f"{k!r} isn't str|Node")


class Edge(ABC):
    @classmethod
    def read(cls, file: Path) -> list[Self]:
        fields = dataclasses.fields(cls)  # type: ignore
        assert len(fields) in (2, 3)
        return (
            [
                cls(Node.parse(dst), Node.parse(src))  # type: ignore
                for dst, src in read_tsv(file)
            ]
            if len(fields) == 2
            else [
                cls(Node.parse(dst), Node.parse(src), int(idx))  # type: ignore
                for dst, src, idx in read_tsv(file)
            ]
        )

    @property
    def nodes(self) -> tuple[Node, Node]:
        fields = dataclasses.fields(self)  # type: ignore
        dst = getattr(self, fields[0].name)
        assert isinstance(dst, Node)
        src = getattr(self, fields[1].name)
        assert isinstance(src, Node)
        return (dst, src)

    @property
    @abstractmethod
    def writes(self) -> tuple[Node, ...]: ...

    @property
    @abstractmethod
    def reads(self) -> tuple[Node, ...]: ...


@dataclass(frozen=True)
class Assign(Edge):
    dest: Node
    source: Node

    @property
    def writes(self) -> tuple[Node, ...]:
        return (self.dest,)

    @property
    def reads(self) -> tuple[Node, ...]:
        return (self.source,)


@dataclass(frozen=True)
class Load(Edge):
    into: Node
    addr: Node

    @property
    def writes(self) -> tuple[Node, ...]:
        return (self.into,)

    @property
    def reads(self) -> tuple[Node, ...]:
        return (self.addr,)


@dataclass(frozen=True)
class Store(Edge):
    addr: Node
    value: Node

    @property
    def writes(self) -> tuple[Node, ...]:
        return ()

    @property
    def reads(self) -> tuple[Node, ...]:
        return (self.addr, self.value)


@dataclass(frozen=True)
class Call(Edge):
    callsite: Node
    callee: Node
    nargs: int

    @property
    def writes(self) -> tuple[Node, ...]:
        return (self.callsite,)

    @property
    def reads(self) -> tuple[Node, ...]:
        return (self.callee,)


@dataclass(frozen=True)
class Return(Edge):
    func: Node
    value: Node

    @property
    def writes(self) -> tuple[Node, ...]:
        return ()

    @property
    def reads(self) -> tuple[Node, ...]:
        return (self.value,)


@dataclass(frozen=True)
class ArgumentDefinition(Edge):
    value: Node
    func: Node
    arg_no: int

    @property
    def writes(self) -> tuple[Node, ...]:
        return (self.value,)

    @property
    def reads(self) -> tuple[Node, ...]:
        return (self.func,)


@dataclass(frozen=True)
class ArgumentSupply(Edge):
    callsite: Node
    value: Node
    arg_no: int

    @property
    def writes(self) -> tuple[Node, ...]:
        return ()

    @property
    def reads(self) -> tuple[Node, ...]:
        return (self.value,)


@dataclass(frozen=True)
class Edges:
    """
    Edge data for a single function
    """

    @classmethod
    def read(cls, p: Path) -> "Edges":
        return cls(
            function=Node.parse(p.name),
            debug_table=DebugTable.read(p / "DebugTable.facts"),
            is_alloca={
                Node.parse(entry[0]) for entry in read_tsv(p / "IsAlloca.facts")
            },
            assign=Assign.read(p / "Assign.facts"),
            load=Load.read(p / "Load.facts"),
            store=Store.read(p / "Store.facts"),
            call=Call.read(p / "Call.facts"),
            ret=Return.read(p / "Return.facts"),
            argument_definition=ArgumentDefinition.read(p / "ArgumentDefinition.facts"),
            argument_supply=ArgumentSupply.read(p / "ArgumentSupply.facts"),
        )

    function: Node
    debug_table: DebugTable
    assign: list[Assign]
    load: list[Load]
    store: list[Store]
    call: list[Call]
    ret: list[Return]
    argument_definition: list[ArgumentDefinition]
    argument_supply: list[ArgumentSupply]
    is_alloca: set[Node]

    def all_edges(
        self,
        *,
        assign: bool = True,
        load: bool = True,
        store: bool = True,
        call: bool = True,
        ret: bool = True,
        argument_definition: bool = True,
        argument_supply: bool = True,
    ) -> Iterator[Edge]:
        return itertools.chain(
            self.assign if assign else [],
            self.load if load else [],
            self.store if store else [],
            self.call if call else [],
            self.ret if ret else [],
            self.argument_definition if argument_definition else [],
            self.argument_supply if argument_supply else [],
        )
