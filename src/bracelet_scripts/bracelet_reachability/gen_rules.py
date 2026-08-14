# mypy: disable-error-code="arg-type"

import sys
from dataclasses import dataclass


class InvalidAnalysisPhaseError(ValueError):
    pass


OUTPUTS: set[str] = set()

print(
    """
//.pragma "magic-transform" "*"

.type Value <: symbol
.type FunctionDefinition <: Value
.type Callsite <: Value

// Substitute
.decl sub(to: Value, from: Value)

.decl IsAlloca(v: Value)
.decl IsSymbol(v: Value)

.input IsSymbol
.input IsAlloca


.decl ValueName(core: Value, v: Value)
.decl Starred(starred: Value, v: Value)

.input ValueName
.input Starred

.decl precious(v: Value)
"""
)

if sys.argv[1] == "phase2":
    print(
        """
            .decl EnclosingFunctionForLocal(func: FunctionDefinition, local: Value)
            .input EnclosingFunctionForLocal

            .decl reachable(x: Value)
            .input reachable
        """
    )


decls_str = """
.decl Assign(to: Value, from: Value)
.decl Load(to: Value, from: Value)
.decl Store(to: Value, from: Value)
.decl Call(caller: Callsite, callee: Value)
.decl Return(function: FunctionDefinition, value: Value)
.decl ArgumentDefinition(value: Value, function: FunctionDefinition, idx: unsigned)
.decl ArgumentSupply(callsite: Callsite, value: Value, idx: unsigned)
"""

print(decls_str)


@dataclass(frozen=True)
class Decl:
    to_ty: str
    from_ty: str
    is_variadic: bool
    body: str


DECLS: dict[str, Decl] = {}
for line in decls_str.split("\n"):
    line = line.strip()
    if line == "":
        continue
    name = line.split()[1].split("(")[0]
    args = line.split("(")[1].split(")")[0].split(",")
    DECLS[name] = Decl(
        to_ty=args[0].split(":")[1].strip(),
        from_ty=args[1].split(":")[1].strip(),
        is_variadic=len(args) == 3,
        body=line.replace(".decl ", ""),
    )


def section(name: str) -> None:
    print()
    print(f"// {name}")


section("Inputs")
for name, decl in DECLS.items():
    print(f".input {name}")

section("isValue")
print(".decl isValue(v: Value)")
for name, decl in DECLS.items():
    idx = ", _" if decl.is_variadic else ""
    print(f"isValue(x) :- {name}(x, _{idx}).")
    print(f"isValue(x) :- {name}(_, x{idx}).")

section("Substitute Relations")
for name, decl in DECLS.items():
    inline = ""  # if f"Sub{name}" in OUTPUTS else " inline"
    OUTPUTS.add(f"PreciousSub{name}")
    print(f".decl Sub{decl.body}" + inline)
    idx = ", idx" if decl.is_variadic else ""
    if decl.to_ty == "Value" and name != "Store":
        # Both sides of Store have "load"/"read" subsitutions.
        print(f"Sub{name}(to2, from{idx}) :- {name}(to, from{idx}), sub(to2, to).")
    if decl.from_ty == "Value":
        print(f"Sub{name}(to, from2{idx}) :- {name}(to, from{idx}), sub(from, from2).")
    print(f".decl PreciousSub{decl.body}")
    print(
        f"PreciousSub{name}(to, from{idx}) :- Sub{name}(to, from{idx}), precious(to), precious(from)."
    )


print(
    """
// We summarize/describe this function in terms of precious(v)
precious(v) :- IsSymbol(v).
precious(v) :- ArgumentDefinition(v, _, _).
precious(v) :- Call(v, _). // track return values from callees
precious(v) :- SubArgumentSupply(_, v, _), IsAlloca(v). // TODO: still inefficient?
precious(v) :- precious(x), ValueName(x, v).
"""
)

if sys.argv[1] == "phase2":
    print(
        """
sub(dstStar, srcStar) :- sub(dst, src), Starred(dstStar, dst), Starred(srcStar, src).
// TODO: this may be quite slow
sub(x, z) :- sub(x, y), sub(y, z).
sub(dst, value) :- Starred(dst, ptr), SubStore(ptr, value).
sub(dst, value) :- Starred(value, ptr), SubLoad(dst, ptr).
    """
    )
elif sys.argv[1] == "phase1":
    print(
        """
sub(dst, value) :- SubLoad(dst, ptr), SubStore(ptr, value).
sub(dst, value) :- Starred(dst, ptr), SubStore(ptr, value).
sub(dst, value) :- Starred(value, ptr), SubLoad(dst, ptr).
    """
    )
else:
    raise InvalidAnalysisPhaseError(sys.argv[1])

print(
    """
SubStore(to2, from) :- Store(to, from), sub(to, to2).

sub(x, x) :- isValue(x).

sub(x, y) :- SubAssign(x, y).

sub(call, returnValue) :- SubCall(call, f), SubReturn(f, returnValue).
sub(calleeArg, callerArg) :-
  SubArgumentSupply(call, callerArg, i),
  SubCall(call, f),
  SubArgumentDefinition(calleeArg, f, i).

.decl PreciousStarred(a: Value, b: Value)
.decl PreciousValueName(a: Value, b: Value)
.decl PreciousIsAlloca(a: Value)

PreciousStarred(a, b) :- Starred(a, b), precious(a).
PreciousValueName(a, b) :- ValueName(a, b), precious(a).
PreciousIsAlloca(a) :- IsAlloca(a), precious(a).
"""
)


if sys.argv[1] == "phase2":
    print(
        """
            reachable(x) :- IsSymbol(x), reachable(f), EnclosingFunctionForLocal(f, cs), PreciousSubCall(cs, x). 
        """
    )

OUTPUTS |= {"PreciousStarred", "PreciousValueName", "PreciousIsAlloca"}

if sys.argv[1] == "phase2":
    OUTPUTS |= {"reachable"}

section("Output")
for x in sorted(OUTPUTS):
    print(f".output {x}")
