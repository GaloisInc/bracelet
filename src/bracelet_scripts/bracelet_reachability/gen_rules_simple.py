from dataclasses import dataclass

OUTPUTS: set[str] = set()

print(
    """
//.pragma "magic-transform" "*"

.type Value <: symbol
.type FunctionDefinition <: Value
.type Callsite <: Value


.decl IsSymbol(v: Value)
.input IsSymbol

.decl EnclosingFunctionForLocal(f: FunctionDefinition, v: Value)
.input EnclosingFunctionForLocal

.decl IsLocal(v: Value)
IsLocal(x) :- EnclosingFunctionForLocal(_, x).

.decl reachable(v: Value)
.input reachable
.output reachable

.decl FunctionName(fn: Value, name: symbol)
.input FunctionName

.decl callgraph(caller: Callsite, callee: Value, kind: symbol)
.output callgraph

.decl ReachableName(fn: Value, name: symbol)
.output ReachableName

.decl FunctionCallGraph(caller: Value, callerN: symbol, callee: Value, calleeN: symbol, kind: symbol)
.output FunctionCallGraph

.decl GlobalAssign(to: Value, from: Value)
.decl GlobalStore(to: Value, from: Value)

.input GlobalAssign
.input GlobalStore

GlobalAssign(x, z) :- GlobalAssign(x, y), GlobalAssign(y, z).
GlobalStore(x, z) :- GlobalStore(x, y), GlobalAssign(y, z).

Store(x, y) :- GlobalStore(x, y).
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

print(".decl isSymbolAddrReferencedFrom(v: Value, f: FunctionDefinition)")
for name, decl in DECLS.items():
    print(f".input {name}")
    if name in ["Call"]:
        continue
    idx = ", _" if decl.is_variadic else ""
    if decl.to_ty == "Value":
        print(
            f"isSymbolAddrReferencedFrom(x, f) :- {name}(x, _{idx}), EnclosingFunctionForLocal(f, x), IsSymbol(x)."
        )
        print(
            f"isSymbolAddrReferencedFrom(x, f) :- {name}(x, y{idx}), EnclosingFunctionForLocal(f, y), IsSymbol(x)."
        )
    if decl.from_ty == "Value" and name != "Store":
        print(
            f"isSymbolAddrReferencedFrom(x, f) :- {name}(_, x{idx}), EnclosingFunctionForLocal(f, x), IsSymbol(x)."
        )
        print(
            f"isSymbolAddrReferencedFrom(x, f) :- {name}(y, x{idx}), EnclosingFunctionForLocal(f, y), IsSymbol(x)."
        )

print(
    """
// Unconditionally say that a store to a local is reachable
isSymbolAddrReferencedFrom(x, f) :-
    Store(dst, x), EnclosingFunctionForLocal(f, x), IsSymbol(x),
    IsLocal(dst).
isSymbolAddrReferencedFrom(x, f) :-
    Store(dst, x), EnclosingFunctionForLocal(f, dst), IsSymbol(x),
    IsLocal(dst).

// Explicit stores to a global are only reachable if that global is reachable
isSymbolAddrReferencedFrom(x, f) :-
    Store(dst, x), EnclosingFunctionForLocal(f, x), IsSymbol(x),
    IsSymbol(dst),
    isSymbolAddrReferenced(dst).
isSymbolAddrReferencedFrom(x, f) :-
    Store(dst, x), EnclosingFunctionForLocal(f, dst), IsSymbol(x),
    IsSymbol(dst),
    isSymbolAddrReferenced(dst).

isSymbolAddrReferenced(x) :- Store(dst, x), isSymbolAddrReferenced(dst).

reachable(callee) :- callgraph(cs, callee, _), EnclosingFunctionForLocal(caller, cs), reachable(caller).

ReachableName(callee, name) :- reachable(callee), FunctionName(callee, name).

.decl isSymbolAddrReferenced(v: Value)
isSymbolAddrReferenced(v) :- isSymbolAddrReferencedFrom(v, f), reachable(f).

// Static calls
callgraph(callsite, callee, "Direct") :-
    Call(callsite, callee),
    IsSymbol(callee).

// Indirect calls through pthread_create
callgraph(callsite, indirect, "Conservative") :-
    Call(callsite, callee),
    IsSymbol(callee),
    FunctionName(callee, "pthread_create"),
    isSymbolAddrReferenced(indirect).

// Indirect calls through std::thread
callgraph(callsite, indirect, "Conservative") :-
    Call(callsite, callee),
    IsSymbol(callee),
    FunctionName(callee, "_ZNSt6thread15_M_start_threadESt10unique_ptrINS_6_StateESt14default_deleteIS1_EEPFvvE"),
    isSymbolAddrReferenced(indirect).

// Indirect Calls
callgraph(callsite, callee, "Conservative") :-
    Call(callsite, callee_ptr),
    IsLocal(callee_ptr),
    isSymbolAddrReferenced(callee).

FunctionCallGraph(caller, callerN, callee, calleeN, kind) :-
   EnclosingFunctionForLocal(caller, callsite),
   callgraph(callsite, callee, kind),
   FunctionName(caller, callerN),
   FunctionName(callee, calleeN).
"""
)
