import argparse
import csv
import json
import operator
import sys
from collections.abc import Callable
from dataclasses import dataclass
from enum import Enum, auto
from pathlib import Path
from typing import Annotated, Any

import cxxfilt  # type: ignore[import-untyped]
from pydantic import BaseModel, ConfigDict, Field
from semver import VersionInfo

# Vuln format

# {
# "cve-id": "",
# "cve-description": "",
# "package-name": "",
# "package-version": "",
# "cwe-id": "",
# "cwe-name": "",
# "affected-function": "",
# "affected-file": ""
# }


def hyphenize(field: str) -> str:
    return field.replace("_", "-")


class HyphenModel(BaseModel):
    model_config = ConfigDict(alias_generator=hyphenize)


class VulnSpec(HyphenModel):
    cve_id: Annotated[str, Field(alias="cve-id")]
    cve_description: str
    package_name: str
    package_version: str
    cwe_id: str
    cwe_name: str
    affected_function: str
    affected_file: str
    additional_required_syms: list[str] = Field(default=[])
    waypoint_syms: list[str] = Field(default=[])


class VSpecOp(Enum):
    GEQ = auto()
    LEQ = auto()
    EQ = auto()
    LT = auto()
    GT = auto()

    def is_lower_bound(self) -> bool:
        return self == VSpecOp.GT or self == VSpecOp.GEQ

    def is_upper_bound(self) -> bool:
        return self == VSpecOp.LEQ or self == VSpecOp.LT

    def operator(self) -> Callable[[VersionInfo, VersionInfo], bool]:
        match self:
            case VSpecOp.GEQ:
                return operator.ge
            case VSpecOp.LEQ:
                return operator.le
            case VSpecOp.EQ:
                return operator.eq
            case VSpecOp.LT:
                return operator.lt
            case VSpecOp.GT:
                return operator.gt


@dataclass
class SingleConstraint:
    op: VSpecOp
    ver: VersionInfo

    def __call__(self, other: VersionInfo) -> bool:
        res = self.op.operator()(other, self.ver)
        return res


@dataclass
class ManyConstraints:
    cons: list[SingleConstraint]

    def __call__(self, other: VersionInfo) -> bool:
        return all(x(other) for x in self.cons)


class VulnerabiliesSpec(HyphenModel):
    vulnerabilities: list[VulnSpec]


VersionConstraint = Callable[[VersionInfo], bool]


def version_satisfies_range(ver: VersionInfo, vrange: list[VersionConstraint]) -> bool:
    res = [x(ver) for x in vrange]
    return any(res)


def parse_op(constraint: str) -> tuple[VSpecOp, str]:
    if constraint.startswith(">="):
        return (VSpecOp.GEQ, constraint[2:])
    elif constraint.startswith("<="):
        return (VSpecOp.LEQ, constraint[2:])
    elif constraint.startswith("="):
        return (VSpecOp.EQ, constraint[1:])
    elif constraint.startswith("<"):
        return (VSpecOp.LT, constraint[1:])
    elif constraint.startswith(">"):
        return (VSpecOp.GT, constraint[1:])
    else:
        return (VSpecOp.EQ, constraint)


def parse_semver(ver: str) -> VersionInfo:
    ver = ver.removeprefix("v")
    ver = ver.split("#")[0] if "#" in ver else ver
    return VersionInfo.parse(ver, optional_minor_and_patch=True)


# TODO: Parsing simply cannot fail...
def parse_version_constraint(constraint: str) -> SingleConstraint:
    (cons_op, verstr) = parse_op(constraint)
    version = parse_semver(verstr)
    return SingleConstraint(cons_op, version)


def parse_purl_cons(constraint_non_stripped: str) -> list[VersionConstraint]:
    constraint = constraint_non_stripped.strip().replace(" ", "")
    curr_group = []
    total: list[list[SingleConstraint]] = []
    for x in constraint.split("|"):
        cons = parse_version_constraint(x)
        if cons.op.is_upper_bound():
            curr_group.append(cons)
            total.append(curr_group)
            curr_group = []
        else:
            if len(curr_group) > 0:
                total.append(curr_group)
                curr_group = []
            if cons.op.is_lower_bound():
                curr_group.append(cons)
            else:
                total.append([cons])

    if len(curr_group) > 0:
        total.append(curr_group)

    return [ManyConstraints(grp) for grp in total]


@dataclass(frozen=True)
class TargetFunction:
    name: str
    addr: str
    cve_id: str
    version_str: str


# todo we should probably honestly parse this
# at least balanced parens
def extract_fname(tname: str) -> str | None:
    try:
        dem = cxxfilt.demangle(tname)
    except cxxfilt.InvalidName:
        return None

    ind = dem.find("(")
    fqn = dem[: (len(dem) if ind < 0 else ind)]
    # last
    return fqn.split("::")[-1]  # type: ignore[no-any-return]


def build_sym_to_address(directory: str) -> dict[str, int]:
    funcs = list(Path(directory).glob("0x*"))
    mp: dict[str, int] = {}
    for func in funcs:
        fname = (func / "function-name.txt").read_text()
        if len(fname) > 0:
            mp[fname] = int(func.name, 16)
    return mp


def find_vulns(directory: str, vulns: VulnerabiliesSpec) -> list[TargetFunction]:
    tfuncs = []
    for vuln in vulns.vulnerabilities:
        constraint = parse_purl_cons(vuln.package_version)
        package_name = vuln.package_name
        target_func = vuln.affected_function
        cveid = vuln.cve_id

        funcs = list(Path(directory).glob("0x*"))

        # first pass grab the addresses that are from a valid BOM

        for func in funcs:
            valid_addrs: dict[str, str] = {}
            with open(func / "SbomTable.facts", "r") as f:
                for row in csv.reader(f, delimiter="\t"):
                    try:
                        v = parse_semver(row[2])
                    except ValueError as e:
                        print(
                            f"Warning: error parsing symbol version ({e}) for {row}",
                            file=sys.stderr,
                        )
                        v = None
                    if (
                        v is not None
                        and row[1] == package_name
                        and version_satisfies_range(v, constraint)
                    ):
                        valid_addrs[row[0]] = row[2]
            with open(func / "DebugTable.facts", "r") as f:
                for row in csv.reader(f, delimiter="\t"):
                    # match on either function name or demangled name
                    # extract_fname can return None in which case
                    # target_func != None
                    if row[0] in valid_addrs:
                        if target_func in [row[1], extract_fname(row[1])]:
                            tfuncs.append(
                                TargetFunction(
                                    target_func, row[0], cveid, valid_addrs[row[0]]
                                )
                            )
                        try:
                            with open(func / "InlineFunctions.facts", "r") as inlined:
                                new_targets: set[TargetFunction] = set()
                                for entry in csv.reader(inlined, delimiter="\t"):
                                    if target_func in [
                                        entry[1],
                                        extract_fname(entry[1]),
                                    ]:
                                        new_targets.add(
                                            TargetFunction(
                                                f"{target_func} (inlined into {row[1]})",
                                                row[0],
                                                cveid,
                                                valid_addrs[row[0]],
                                            )
                                        )
                                tfuncs.extend(new_targets)
                        except FileNotFoundError:
                            # Function has no inlined callees
                            pass

    return tfuncs


def main() -> None:
    prsr = argparse.ArgumentParser("Find target address for vulnerability")
    prsr.add_argument("directory")
    prsr.add_argument("vuln")
    args: Any = prsr.parse_args()

    with open(args.vuln, "r") as f:
        vulns_js = json.load(f)
    vulns = VulnerabiliesSpec(**vulns_js)

    print(find_vulns(args.directory, vulns))


if __name__ == "__main__":
    main()
