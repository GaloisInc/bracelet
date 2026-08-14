#!/usr/bin/env python3

"""
This generates BraceletRuntimeStructs_{c,lldb,llvm}.h

This allows for easy interop between generating C structs at runtime,
as well as at build-time with LLVM, and parsing them from a coredump with LLDB.

See the struct and function definitions below for how this code is used.
"""

import dataclasses
import enum
import itertools
import math
import re
import shutil
import struct
import subprocess
from abc import ABC, abstractmethod
from collections.abc import Callable, Iterator
from dataclasses import dataclass
from functools import cached_property, partial
from pathlib import Path
from random import Random
from types import NoneType
from typing import Any, ForwardRef, Literal, Self


class UnsupportedRuntimeTypeError(TypeError):
    pass


@dataclass(frozen=True)
class Layout:
    size: int
    align: int

    def __post_init__(self) -> None:
        assert self.size >= 0
        assert self.align >= 1
        # assert that the alignment is a power of 2 (or 1)
        assert self.align.bit_count() == 1

    @cached_property
    def align_log2(self) -> int:
        return int(math.log2(self.align))


class Type[T](ABC):
    @abstractmethod
    def unpack(self, data: memoryview) -> T: ...

    def pack(self, t: T) -> bytearray:
        out = bytearray(self.layout.size)
        self.pack_into(t, memoryview(out))
        return out

    @abstractmethod
    def pack_into(self, t: T, data: memoryview) -> None:
        """
        Write t into data
        """

    @abstractmethod
    def random(self, rng: Random) -> T:
        """
        Return a random T
        """

    @property
    @abstractmethod
    def layout(self) -> Layout: ...

    @abstractmethod
    def type_dependencies(self) -> Iterator["Type[Any]"]: ...

    @property
    @abstractmethod
    def c_type_name(self) -> str:
        """
        What's needed to declare the type.
        """

    @property
    @abstractmethod
    def c_type_declaration(self) -> str:
        """
        Declare anything neccessary for the type.
        """

    @property
    @abstractmethod
    def llvm_type(self) -> str:
        """
        What's the expression for the LLVM type?

        ctx is an LLVMContext&
        struct types can be accessed by name
        """

    @property
    @abstractmethod
    def llvm_type_type(self) -> str:
        """
        What's the type of the LLVM type?

        e.g. "llvm::IntegerType*"
        """

    @property
    @abstractmethod
    def lldb_type(self) -> str:
        """
        What's the expression for the LLDB type?
        """

    @abstractmethod
    def lldb_ptr_load(self, dst: str, offset: str) -> str:
        """
        Emit code to adjust pointers for {dst} which is loaded from address {offset}
        """

    @abstractmethod
    def test_decode_code(self, value: str, t: T) -> str:
        """
        Emit C code to assert that the C value `value` equals `t`
        """


@dataclass(frozen=True)
class IntTy(Type[int]):
    bits: int
    signed: bool

    def unpack(self, data: memoryview) -> int:
        out = self.struct_format.unpack(data)[0]
        assert isinstance(out, int)
        return out

    def pack_into(self, t: int, data: memoryview) -> None:
        self.struct_format.pack_into(data, 0, t)

    def random(self, rng: Random) -> int:
        return rng.randrange(0, 1 << self.bits)

    def __post_init__(self) -> None:
        assert self.bits in (8, 16, 32, 64)

    @property
    def bytes(self) -> int:
        return self.bits // 8

    @cached_property
    def layout(self) -> Layout:
        return Layout(size=self.bytes, align=self.bytes)

    @property
    def struct_char(self) -> str:
        ch = {1: "b", 2: "h", 4: "i", 8: "l"}[self.bytes]
        return ch.lower() if self.signed else ch.upper()

    @cached_property
    def struct_format(self) -> struct.Struct:
        return struct.Struct(self.struct_char)

    def type_dependencies(self) -> Iterator[Type[Any]]:
        return iter(())

    @property
    def c_type_name(self) -> str:
        return ("" if self.signed else "u") + f"int{self.bits}_t"

    @property
    def lldb_type(self) -> str:
        return self.c_type_name

    @property
    def c_type_declaration(self) -> str:
        return ""

    @cached_property
    def llvm_type(self) -> str:
        return f"llvm::IntegerType::get(ctx, {self.bits})"

    @property
    def llvm_type_type(self) -> str:
        return "llvm::IntegerType*"

    def test_decode_code(self, value: str, t: int) -> str:
        suffix = "u" if t >= 0 else ""
        return f"assert({value} == {t}{suffix}ll);"

    def lldb_ptr_load(self, dst: str, offset: str) -> str:
        return ""


type I8 = int
type I16 = int
type I32 = int
type I64 = int
type U8 = int
type U16 = int
type U32 = int
type U64 = int

INT_TYPES = {
    I8: IntTy(8, True),
    I16: IntTy(16, True),
    I32: IntTy(32, True),
    I64: IntTy(64, True),
    U8: IntTy(8, False),
    U16: IntTy(16, False),
    U32: IntTy(32, False),
    U64: IntTy(64, False),
}


@dataclass(frozen=True)
class ArrayTy[T](Type[tuple[T, ...]]):
    of: Type[T]
    count: int

    def __post_init__(self) -> None:
        assert self.count > 0
        assert isinstance(self.count, int)

    def unpack(self, data: memoryview) -> tuple[T, ...]:
        size = self.of.layout.size
        assert len(data) == self.count * size
        return tuple(
            self.of.unpack(data[i * size : (i + 1) * size]) for i in range(self.count)
        )

    def pack_into(self, t: tuple[T, ...], data: memoryview) -> None:
        sz = self.of.layout.size
        for elem in t:
            elem_data = data[0:sz]
            data = data[sz:]
            self.of.pack_into(elem, elem_data)

    def random(self, rng: Random) -> tuple[T, ...]:
        return tuple(self.of.random(rng) for _ in range(self.count))

    @cached_property
    def layout(self) -> Layout:
        element_layout = self.of.layout
        return Layout(
            size=element_layout.size * self.count,
            align=element_layout.align,
        )

    def type_dependencies(self) -> Iterator[Type[Any]]:
        return iter((self.of,))

    @cached_property
    def c_type_name(self) -> str:
        return f"array_{self.of.c_type_name}_{self.count}"

    @property
    def c_type_declaration(self) -> str:
        return f"typedef {self.of.c_type_name} {self.c_type_name}[{self.count}];"

    @cached_property
    def llvm_type(self) -> str:
        return f"llvm::ArrayType::get({self.of.llvm_type}, {self.count})"

    @property
    def llvm_type_type(self) -> str:
        return "llvm::ArrayType*"

    @cached_property
    def lldb_type(self) -> str:
        return f"std::array<{self.of.lldb_type}, {self.count}>"

    def test_decode_code(self, value: str, t: tuple[T, ...]) -> str:
        assert len(t) == self.count
        t_subset = list(enumerate(t))
        if len(t_subset) > 64:
            # Let's not check _every_ array element due to C compile times
            t_subset = t_subset[0:32] + t_subset[-32:]
        return "\n".join(
            self.of.test_decode_code(f"{value}[{i}]", t_i) for i, t_i in t_subset
        )

    def lldb_ptr_load(self, dst: str, offset: str) -> str:
        return (
            f"for (size_t i = 0; i < {self.count}; i++) {{"
            + f"size_t offset = {offset} + i*{self.of.layout.size};"
            + f"auto& dst = {dst}[i];"
            + self.of.lldb_ptr_load("dst", "offset")
            + "}"
        )


@dataclass(frozen=True)
class EnumTy[T: "Enum"](Type[T]):
    """
    We store enums as ints starting at 1, so that 0 remains an undefined value.
    """

    name: str
    value_cls: type[T]

    @cached_property
    def is_flags(self) -> bool:
        return issubclass(self.value_cls, enum.Flag)

    @cached_property
    def variants(self) -> tuple[T, ...]:
        return tuple(self.value_cls)  # type: ignore

    @cached_property
    def variant_packing(self) -> dict[T, int]:
        return {
            v: 1 << i if self.is_flags else 1 + i for i, v in enumerate(self.variants)
        }

    @cached_property
    def underlying_int(self) -> IntTy:
        max_value = 1 << len(self.variants) if self.is_flags else len(self.variants) + 1
        return min(
            (
                int_ty
                for int_ty in INT_TYPES.values()
                if not int_ty.signed and (max_value < (1 << int_ty.bits))
            ),
            key=lambda int_ty: int_ty.bits,
        )

    def unpack(self, data: memoryview) -> T:
        raw = self.underlying_int.unpack(data)
        if self.is_flags:
            out = self.value_cls(0)  # type: ignore
            for v, mask in self.variant_packing.items():
                if (raw & mask) != 0:
                    out |= v  # type: ignore
            return out
        else:
            raw -= 1
            return self.variants[raw]

    def pack_into(self, t: T, data: memoryview) -> None:
        if self.is_flags:
            raw = 0
            for v in t:
                raw |= self.variant_packing[v]
        else:
            raw = self.variant_packing[t]
        self.underlying_int.pack_into(raw, data)

    def random(self, rng: Random) -> T:
        if self.is_flags:
            out = self.value_cls(0)  # type: ignore
            for v in self.variants:
                if bool(rng.getrandbits(1)):
                    out |= v  # type: ignore
            return out
        else:
            return rng.choice(self.variants)

    @cached_property
    def layout(self) -> Layout:
        return self.underlying_int.layout

    def type_dependencies(self) -> Iterator[Type[Any]]:
        return iter((self.underlying_int,))

    @property
    def c_type_name(self) -> str:
        return self.name

    @property
    def c_type_declaration(self) -> str:
        return f"typedef {self.underlying_int.c_type_name} {self.name};" + "\n".join(
            f"#define {self.name}_{variant.name} {encoding}"
            for variant, encoding in self.variant_packing.items()
        )

    @property
    def llvm_type(self) -> str:
        return self.name

    @property
    def llvm_type_type(self) -> str:
        return "llvm::IntegerType*"

    @property
    def lldb_type(self) -> str:
        return self.name

    def lldb_ptr_load(self, dst: str, offset: str) -> str:
        return ""

    def test_decode_code(self, value: str, t: T) -> str:
        return f"{value} == {self.name}_{t.name}"


@dataclass(frozen=True)
class StructTy[T: "Struct"](Type[T]):
    name: str
    fields_constructor: Callable[[], dict[str, Type[Any]]]
    value_cls: type[T]

    def __post_init__(self) -> None:
        assert len(self.name) > 0

    def unpack(self, data: memoryview) -> T:
        return self.value_cls(
            *tuple(
                ty.unpack(data[offset : offset + ty.layout.size])
                for ty, offset in zip(self.field_types, self.field_offsets)
            )
        )

    def pack_into(self, t: T, data: memoryview) -> None:
        for name, ty, offset in zip(
            self.field_names, self.field_types, self.field_offsets
        ):
            ty.pack_into(getattr(t, name), data[offset : offset + ty.layout.size])

    def random(self, rng: Random) -> T:
        return self.value_cls(*tuple(ty.random(rng) for ty in self.field_types))

    @cached_property
    def fields(self) -> dict[str, Type[Any]]:
        out = self.fields_constructor()
        assert len(out) > 0
        return out

    @cached_property
    def field_names(self) -> tuple[str, ...]:
        return tuple(self.fields.keys())

    @cached_property
    def field_types(self) -> tuple[Type[Any], ...]:
        return tuple(self.fields.values())

    @cached_property
    def field_offsets(self) -> tuple[int, ...]:
        offsets = []
        offset = 0
        for ty in self.fields.values():
            # Who needs math when you have loops!
            while offset % ty.layout.align != 0:
                offset += 1
            offsets.append(offset)
            offset += ty.layout.size
        return tuple(offsets)

    @cached_property
    def layout(self) -> Layout:
        size = 0
        if len(self.fields) > 0:
            size = self.field_offsets[-1] + self.field_types[-1].layout.size
            while size % self.field_types[0].layout.align != 0:
                size += 1
        return Layout(
            size=size,
            align=max((ty.layout.align for ty in self.field_types), default=1),
        )

    def type_dependencies(self) -> Iterator[Type[Any]]:
        return iter(self.field_types)

    @property
    def c_type_name(self) -> str:
        return self.name

    @property
    def c_type_declaration(self) -> str:
        return (
            "struct "
            + self.name
            + "{\n"
            + "\n".join(
                f"  {ty.c_type_name} {name};" for name, ty in self.fields.items()
            )
            + "\n};"
        )

    @property
    def llvm_type(self) -> str:
        return self.name

    @property
    def llvm_type_type(self) -> str:
        return "llvm::StructType*"

    @property
    def lldb_type(self) -> str:
        return self.name

    def lldb_ptr_load(self, dst: str, offset: str) -> str:
        return "\n".join(
            ty.lldb_ptr_load(f"{dst}.{field}", f"{offset} + {field_offset}")
            for field, ty, field_offset in zip(
                self.field_names, self.field_types, self.field_offsets
            )
        )

    def test_decode_code(self, value: str, t: T) -> str:
        return "\n".join(
            ty.test_decode_code(f"{value}.{field}", getattr(t, field))
            for field, ty in zip(self.field_names, self.field_types)
        )


@dataclass(frozen=True)
class PointerTy[T](Type["Pointer[T]"]):
    of: Type[T] | None
    const: bool = dataclasses.field(default=False)

    def unpack(self, data: memoryview) -> "Pointer[T]":
        address = INT_TYPES[U64].unpack(data)
        return Pointer(address=address)

    def pack_into(self, t: "Pointer[T]", data: memoryview) -> None:
        INT_TYPES[U64].pack_into(t.address, data)

    def random(self, rng: Random) -> "Pointer[T]":
        return Pointer(
            (
                rng.randrange(0, 1 << 64)
                << (self.of.layout.align_log2 - 1 if self.of else 0)
            )
            & 0xFFFFFFFFFFFFFFFF
        )

    @cached_property
    def layout(self) -> Layout:
        return INT_TYPES[U64].layout

    def type_dependencies(self) -> Iterator[Type[Any]]:
        if self.of:
            yield self.of

    @cached_property
    def c_type_name(self) -> str:
        const = "const_" if self.const else ""
        if self.of:
            return f"{const}ptr_to_{self.of.c_type_name}"
        else:
            return f"{const}void_ptr_t"

    @property
    def c_type_declaration(self) -> str:
        const = "const " if self.const else ""
        if self.of:
            return f"typedef {const}{self.of.c_type_name}* {self.c_type_name};"
        else:
            return f"typedef {const}void* {self.c_type_name};"

    @property
    def llvm_type(self) -> str:
        return "llvm::PointerType::getUnqual(ctx)"

    @property
    def llvm_type_type(self) -> str:
        return "llvm::PointerType*"

    @cached_property
    def lldb_type(self) -> str:
        inner = "void" if self.of is None else self.of.lldb_type
        return f"Pointer<{inner}>"

    def test_decode_code(self, value: str, t: "Pointer[T]") -> str:
        return f"assert(((uintptr_t){value}) == {t.address}ULL);"

    def lldb_ptr_load(self, dst: str, offset: str) -> str:
        return (
            f"BRACELET_TRY(obj.resolvePointers(boost::span(&{dst}.ptr, 1), {offset}));"
        )


@dataclass(frozen=True)
class AtomicTy[T](Type[T]):
    of: Type[T]

    def unpack(self, data: memoryview) -> T:
        return self.of.unpack(data)

    def pack_into(self, t: T, data: memoryview) -> None:
        self.of.pack_into(t, data)

    def random(self, rng: Random) -> T:
        return self.of.random(rng)

    @cached_property
    def layout(self) -> Layout:
        return self.of.layout

    def type_dependencies(self) -> Iterator[Type[Any]]:
        yield self.of

    @cached_property
    def c_type_name(self) -> str:
        return f"atomic_of_{self.of.c_type_name}"

    @property
    def c_type_declaration(self) -> str:
        return f"typedef _Atomic({self.of.c_type_name}) {self.c_type_name};"

    @property
    def llvm_type(self) -> str:
        return self.of.llvm_type

    @property
    def llvm_type_type(self) -> str:
        return self.of.llvm_type_type

    @property
    def lldb_type(self) -> str:
        return self.of.lldb_type

    def lldb_ptr_load(self, dst: str, offset: str) -> str:
        return self.of.lldb_ptr_load(dst, offset)

    def test_decode_code(self, value: str, t: T) -> str:
        return self.of.test_decode_code(f"{value}.load()", t)


type Atomic[T] = T
type Array[T, N] = tuple[T, ...]


@dataclass(frozen=True)
class Pointer[T]:
    address: int


STRUCTS_BY_NAME: dict[str, type["Struct"]] = {}
_STRUCT_TYPES_BY_NAME: dict[str, StructTy[Any]] = {}


class Struct(ABC):
    def __init_subclass__(cls, **kwargs: Any) -> None:
        super().__init_subclass__(**kwargs)
        STRUCTS_BY_NAME[cls.__name__] = cls
        _STRUCT_TYPES_BY_NAME[cls.__name__] = StructTy(
            cls.__name__, partial(_struct_fields, cls), cls
        )

    @classmethod
    def type(cls) -> StructTy[Self]:
        return _STRUCT_TYPES_BY_NAME[cls.__name__]


ENUMS_BY_NAME: dict[str, type["Enum"]] = {}


class Enum(ABC):
    def __init_subclass__(cls, **kwargs: Any) -> None:
        super().__init_subclass__(**kwargs)
        ENUMS_BY_NAME[cls.__name__] = cls

    @classmethod
    def type(cls) -> EnumTy[Self]:
        return EnumTy(cls.__name__, cls)

    @property
    @abstractmethod
    def name(self) -> str:
        pass

    @classmethod
    @abstractmethod
    def __iter__(cls) -> Iterator[Self]: ...


def _make_type(ty: Any) -> Type[Any]:
    int_ty = INT_TYPES.get(ty)
    if int_ty is not None:
        return int_ty
    if hasattr(ty, "__origin__"):
        if ty.__origin__ == Atomic:
            [of] = ty.__args__
            return AtomicTy(_make_type(of))
        elif ty.__origin__ == Pointer:
            [of] = ty.__args__
            return PointerTy(None if of == NoneType else _make_type(of))
        elif ty.__origin__ == Array:
            of, count_arg = ty.__args__
            [count_arg_value] = count_arg.__args__
            if isinstance(count_arg_value, int):
                count = count_arg_value
            elif isinstance(count_arg_value, str):
                count = globals()[count_arg_value]
                assert isinstance(count, int)
            else:
                raise UnsupportedRuntimeTypeError(
                    f"Cannot handle array count {count_arg}"
                )
            return ArrayTy(_make_type(of), count)
    elif isinstance(ty, ForwardRef):
        return _STRUCT_TYPES_BY_NAME[ty.__forward_arg__]
    elif issubclass(ty, Struct):
        return _STRUCT_TYPES_BY_NAME[ty.__name__]
    raise UnsupportedRuntimeTypeError(f"Unknown type {ty} ({type(ty)})")


def _struct_fields(struct_cls: type[Struct]) -> dict[str, Type[Any]]:
    out = {}
    for field in dataclasses.fields(struct_cls):  # type: ignore
        out[field.name] = _make_type(field.type)
    return out


@dataclass(frozen=True)
class BraceletTraceSite(Struct):
    function: Pointer[None]
    local_idx: U32


assert BraceletTraceSite.type().layout.align >= 2


@dataclass(frozen=True)
class BraceletTraceEdge(Struct):
    # This is the address of a BraceletTraceSite
    # If NULL, the edge is not present.
    # The least significant bit of the address should be ignored (it's used for bookkeeping).
    trace_site: U64
    # This is either:
    # 1. Pointers to BraceletTraceSite entries (in the trace site section)
    # 2. OR pointers into a loaded ELF segment
    value: U64


DLSYM_PAGE_SIZE = 16384  # 16KiB
DLSYM_PAGE_NUM_POINTERS = (DLSYM_PAGE_SIZE // 8) - 2


@dataclass(frozen=True)
class DlsymPage(Struct):
    next: Atomic[Pointer["DlsymPage"]]
    count: Atomic[U64]
    pointers: Array[Atomic[Pointer[None]], Literal["DLSYM_PAGE_NUM_POINTERS"]]


assert DlsymPage.type().layout.size == DLSYM_PAGE_SIZE

ALL_STRUCTS: list[StructTy[Struct]] = [
    struct.type() for struct in STRUCTS_BY_NAME.values()
]


@dataclass(frozen=True)
class RuntimeFunction:
    args: dict[str, Type[Any]]
    return_type: Type[Any] | None


RUNTIME_FUNCTIONS = {
    "braceletTraceBuffer": RuntimeFunction(
        {
            "site": PointerTy(BraceletTraceSite.type()),
            "ptr": PointerTy(None, const=True),
            "size": INT_TYPES[U64],
        },
        None,
    ),
    "braceletTraceWord": RuntimeFunction(
        {
            "site": PointerTy(BraceletTraceSite.type()),
            "word": INT_TYPES[U64],
        },
        None,
    ),
    "braceletTraceTagAllocation": RuntimeFunction(
        {
            "site": PointerTy(BraceletTraceSite.type(), const=True),
            "ptr": PointerTy(None),
        },
        None,
    ),
    "braceletTraceAllocaAllocate": RuntimeFunction(
        {
            "site": PointerTy(BraceletTraceSite.type(), const=True),
            "size": INT_TYPES[U64],
        },
        PointerTy(None),
    ),
    "braceletTraceAllocaFree": RuntimeFunction(
        {
            "ptr": PointerTy(None),
        },
        None,
    ),
}


def visit_all_types(cb: Callable[[Type[Any]], None]) -> None:
    """
    Visit all types in topological order
    """
    visited = set()

    def visit(ty: Type[Any]) -> None:
        if ty in visited:
            return
        visited.add(ty)
        for child in ty.type_dependencies():
            visit(child)
        cb(ty)

    for ty in itertools.chain(
        ALL_STRUCTS,
        itertools.chain.from_iterable(
            itertools.chain([f.return_type] if f.return_type else [], f.args.values())
            for f in RUNTIME_FUNCTIONS.values()
        ),
    ):
        visit(ty)


if __name__ == "__main__":
    import click
    import jinja2

    template_env = jinja2.Environment(
        loader=jinja2.PackageLoader("RuntimeFormat"),
        autoescape=False,
    )
    # If you add a template here, add it to the dependencies list in meson.build
    llvm_template = template_env.get_template("llvm.tmpl")
    lldb_template = template_env.get_template("lldb.tmpl")
    test_template = template_env.get_template("test.tmpl")

    def clang_format(code: str) -> str:
        code = re.sub(
            r"\n[\s\n]*\n",
            "\n",
            code,
        )
        if shutil.which("clang-format"):
            return subprocess.run(
                ["clang-format"],
                input=code.encode("utf-8"),
                stdout=subprocess.PIPE,
                check=False,
            ).stdout.decode("utf-8")
        return code

    class OutputKind(enum.Enum):
        C = enum.auto()
        LLVM = enum.auto()
        LLDB = enum.auto()
        Test = enum.auto()

    @click.command()
    @click.option("--out", "-o", type=click.Path(path_type=Path), required=True)
    @click.argument("kind", type=click.Choice(OutputKind, case_sensitive=False))
    def generate(kind: OutputKind, out: Path) -> None:
        structs_in_topo_order = []

        def order_cb(ty: Type[Any]) -> None:
            if isinstance(ty, StructTy):
                structs_in_topo_order.append(ty)

        visit_all_types(order_cb)
        out_buf = ""
        match kind:
            case OutputKind.C:
                out_buf += "#pragma once\n#include <stdint.h>\n"
                out_buf += (
                    "#ifdef __cplusplus\n#include <atomic>\n"
                    + "#define _Atomic(T) std::atomic<T>\n#endif\n"
                )
                for ty in structs_in_topo_order:
                    out_buf += f"typedef struct {ty.name} {ty.name};\n"

                def cb(ty: Type[Any]) -> None:
                    nonlocal out_buf
                    td = ty.c_type_declaration
                    if len(td) > 0:
                        out_buf += f"{td}\n"

                visit_all_types(cb)
                for f_name, f in RUNTIME_FUNCTIONS.items():
                    rt = f.return_type.c_type_name if f.return_type else "void"
                    out_buf += 'extern\n#ifdef __cplusplus\n"C"\n#endif\n'
                    out_buf += f"{rt} {f_name}("
                    out_buf += ", ".join(
                        f"{arg_ty.c_type_name} {arg_name}"
                        for arg_name, arg_ty in f.args.items()
                    )
                    out_buf += ");\n"
                out_buf += "#ifdef __cplusplus\n#undef _Atomic\n#endif\n"
            case OutputKind.LLVM:
                out_buf += clang_format(
                    llvm_template.render(
                        ALL_STRUCTS=structs_in_topo_order,
                        RUNTIME_FUNCTIONS=RUNTIME_FUNCTIONS,
                        zip=zip,
                    ),
                )
            case OutputKind.LLDB:
                out_buf += clang_format(
                    lldb_template.render(
                        ALL_STRUCTS=structs_in_topo_order,
                    ),
                )
            case OutputKind.Test:
                rng = Random(b"a delightful lil seed")
                test_blobs = {
                    struct_ty: [struct_ty.pack(struct_ty.random(rng)) for _ in range(3)]
                    for struct_ty in structs_in_topo_order
                }
                out_buf += test_template.render(
                    ALL_STRUCTS=structs_in_topo_order,
                    zip=zip,
                    test_blobs=test_blobs,
                )
        if (not out.exists()) or out.read_text() != out_buf:
            # meson has restat enabled for this command, so if we don't rewrite the file
            # then ninja will cancel any downstream jobs as an optimization
            out.write_text(out_buf)

    generate()
