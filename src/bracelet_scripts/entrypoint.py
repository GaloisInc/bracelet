import argparse
import json
import shutil
import sys
import tempfile
from abc import ABC, abstractmethod
from collections import defaultdict
from dataclasses import dataclass
from os import PathLike
from pathlib import Path
from typing import Any

from bracelet_scripts import edges_utils, get_target_address
from bracelet_scripts.bracelet_reachability import (
    datalog_callgraph_output,
    run_it,
    run_it_simple,
)
from bracelet_scripts.bracelet_reachability.cg_lib import CallGraph
from bracelet_scripts.data_format import (
    ReachabilityResults,
    ReachabilityVuln,
    VulnClassification,
)
from bracelet_scripts.get_target_address import (
    TargetFunction,
    VulnerabiliesSpec,
    VulnSpec,
)
from bracelet_scripts.points_to.entrypoint_support import SVFPaths, svf_run_on_path


@dataclass
class ReachabilityComment:
    message: str


class FunctionFilter(ABC):
    @abstractmethod
    def reclassify(
        self, vuln_spec: VulnSpec, tfunc: TargetFunction
    ) -> ReachabilityVuln | ReachabilityComment: ...


class AddressSetFFilter(FunctionFilter):
    def __init__(
        self, aset: set[int], cg: CallGraph, symbol_to_addr: dict[str, int]
    ) -> None:
        super().__init__()
        self.aset = aset
        self.cg = cg
        self.symbol_to_addr = symbol_to_addr

    def reclassify(
        self, vuln_spec: VulnSpec, tfunc: TargetFunction
    ) -> ReachabilityVuln | ReachabilityComment:
        target_addr = int(tfunc.addr, base=16)
        if target_addr not in self.aset:
            return ReachabilityVuln(
                cve_id=vuln_spec.cve_id,
                classification=VulnClassification.unreachable,
                justification=f"Target function {tfunc.name} is present at version {tfunc.version_str} at addr {tfunc.addr}, however the function is not in the callgraph produced by pointer analysis",
            )
        else:
            waypoint_addrs = []
            for waypoint in vuln_spec.waypoint_syms:
                addr = self.symbol_to_addr.get(waypoint, None)
                if addr is None:
                    return ReachabilityVuln(
                        cve_id=vuln_spec.cve_id,
                        classification=VulnClassification.unreachable,
                        justification=f"Target function {tfunc.name} is present at version {tfunc.version_str} at addr {tfunc.addr}, however the required waypoint {waypoint} is not.",
                    )
                waypoint_addrs.append(f"0x{addr:016x}")
            cg_path = self.cg.print_path_from_main(tfunc.addr, waypoint_addrs)
            if cg_path is not None:
                return ReachabilityComment("Reachable on callgraph path: " + cg_path)
            else:
                cg_path = self.cg.print_path_from_main(tfunc.addr)
                return ReachabilityVuln(
                    cve_id=vuln_spec.cve_id,
                    classification=VulnClassification.unreachable,
                    justification=f"Target function {tfunc.name} is present at version {tfunc.version_str} at addr {tfunc.addr}, however the function is not reachable following the required waypoints: {vuln_spec.waypoint_syms}. An alternative callgraph path not following the waypoints is: {cg_path}.",
                )


class AdditionalFuncsFiliter(FunctionFilter):
    def __init__(self, aset: set[int], symbol_to_addr: dict[str, int]) -> None:
        super().__init__()
        self.allowed_symbols = set()
        for k, v in symbol_to_addr.items():
            if v in aset:
                self.allowed_symbols.add(k)

    def reclassify(
        self, vuln_spec: VulnSpec, tfunc: TargetFunction
    ) -> ReachabilityVuln | ReachabilityComment:
        for req_sym in vuln_spec.additional_required_syms:
            if req_sym not in self.allowed_symbols:
                return ReachabilityVuln(
                    cve_id=vuln_spec.cve_id,
                    classification=VulnClassification.unreachable,
                    justification=f"Target function {tfunc.name} is present at version {tfunc.version_str} at addr {tfunc.addr}, however an additional required function is not reachable {req_sym}",
                )
        if not vuln_spec.additional_required_syms:
            return ReachabilityComment(
                "No additional symbols were required to be reachable."
            )
        else:
            return ReachabilityComment(
                "Additional symbols also deemed reachable: "
                + ", ".join(vuln_spec.additional_required_syms)
            )


class NoopFunctionFilter(FunctionFilter):
    def reclassify(
        self, vuln_spec: VulnSpec, tfunc: TargetFunction
    ) -> ReachabilityVuln | ReachabilityComment:
        return ReachabilityComment("")


class ManyFilters(FunctionFilter):
    def __init__(self, lst: list[FunctionFilter]) -> None:
        super().__init__()
        self.lst = lst

    def reclassify(
        self, vuln_spec: VulnSpec, tfunc: TargetFunction
    ) -> ReachabilityVuln | ReachabilityComment:
        comment_buffer = ""
        for fltr in self.lst:
            res = fltr.reclassify(vuln_spec, tfunc)
            if isinstance(res, ReachabilityVuln):
                return res
            else:
                comment_buffer += res.message + "\n"
        return ReachabilityComment(comment_buffer)


class ReachabilityAnalysis(ABC):
    @abstractmethod
    def analyze_reachable_addresses(self, pth: Path) -> tuple[set[int], CallGraph]: ...


class DatalogAnalysis(ReachabilityAnalysis):
    def __init__(self) -> None:
        super().__init__()

    @abstractmethod
    def callgraph_input_name(self) -> str: ...

    @abstractmethod
    def run_on_path(self, pth: Path, output_path: Path) -> None: ...

    def analyze_reachable_addresses(self, pth: Path) -> tuple[set[int], CallGraph]:
        output_path = pth / "output"
        self.run_on_path(pth, output_path)
        reachability_csv = output_path / "reachable.csv"
        with open(reachability_csv, "r") as f:
            aset = set()
            for ln in f:
                if ":" not in ln:
                    # The SVF analysis results include results that aren't nodes due to signatures
                    try:
                        aset.add(int(ln.strip(), 16))
                    except ValueError:
                        print(
                            f"Warning: skipped adding {ln.strip()} to reachable.csv",
                            file=sys.stderr,
                        )
            cg = CallGraph(output_path, self.callgraph_input_name())
            return (aset, cg)


class SimpleAnalysis(DatalogAnalysis):
    def __init__(self) -> None:
        super().__init__()

    def callgraph_input_name(self) -> str:
        return "callgraph.csv"

    def run_on_path(self, pth: Path, output_path: Path) -> None:
        return run_it_simple.run_on_path(pth, output_path)


class PointerAnalysis(DatalogAnalysis):
    def __init__(
        self, bracelet_edges: str | PathLike[str], bracelet_clang: str | PathLike[str]
    ) -> None:
        super().__init__()
        self.bracelet_edges = bracelet_edges
        self.bracelet_clang = bracelet_clang

    def callgraph_input_name(self) -> str:
        return "PreciousSubCall.csv"

    def run_on_path(self, pth: Path, output_path: Path) -> None:
        return run_it.run_on_path(
            pth, output_path, self.bracelet_edges, self.bracelet_clang  # type: ignore[arg-type]
        )


class SavedAnalysis(DatalogAnalysis):
    def __init__(self, saved_output: Path, saved_input_name: str | None) -> None:
        super().__init__()
        self.saved_output = saved_output
        self.saved_input_name = (
            saved_input_name if saved_input_name is not None else "cg.csv"
        )

    def callgraph_input_name(self) -> str:
        return self.saved_input_name

    def run_on_path(self, pth: Path, output_path: Path) -> None:
        pass


class SvfPointerAnalysis(DatalogAnalysis):
    def __init__(
        self,
        bracelet_points_to: Path,
        svf_paths: SVFPaths,
        conservative: bool,
        snapshot: Path,
        target_pid: int,
        save_missing: Path | None,
        save_pts: Path | None,
    ) -> None:
        super().__init__()
        self.bracelet_points_to = bracelet_points_to
        self.svf_paths = svf_paths
        self.conservative = conservative
        self.snapshot = snapshot
        self.target_pid = target_pid
        self.save_missing = save_missing
        self.save_pts = save_pts

    def callgraph_input_name(self) -> str:
        return "cg.csv"

    def run_on_path(self, pth: Path, output_path: Path) -> None:
        svf_run_on_path(
            self.bracelet_points_to,
            self.svf_paths,
            self.conservative,
            self.snapshot,  # type: ignore[arg-type]
            self.target_pid,
            pth,
            output_path,
            self.save_missing,
            self.save_pts,
        )


def merge_vulns(
    curr: ReachabilityVuln | None, candidate: ReachabilityVuln
) -> ReachabilityVuln:
    if curr is None:
        return candidate
    # note: we assume definitely reachable can never appear nor unable to assess
    if curr.classification == candidate.classification:
        curr.justification += f"\n Additional justification from separate analyzed function: {candidate.justification}"
        return curr
    if candidate.classification == VulnClassification.potentially_reachable:
        # in this case candidate is the only reachable one so return
        return candidate
    # curr is the only reachable one so return
    return curr


def main() -> None:
    prsr = argparse.ArgumentParser("Analysis entrypoint")
    prsr.add_argument("--bracelet-edges", default="bracelet-edges")
    prsr.add_argument("--bracelet-clang", default=None, type=Path)
    prsr.add_argument("--bracelet-points-to", default=None, type=Path)
    prsr.add_argument("--svf-path", default=Path("/opt/svf"), type=Path)
    prsr.add_argument("--svf-clang", default=None, type=Path)
    prsr.add_argument("--svf-llvm", default=None, type=Path)
    prsr.add_argument("--pid", default=None)
    prsr.add_argument("--save-wdir", action="store_true", default=False)
    prsr.add_argument("--vuln-json", required=False)
    prsr.add_argument("snapshot")
    prsr.add_argument("--run-cg-filter", default=False, action="store_true")
    prsr.add_argument("--pointer-analysis", default=False, action="store_true")
    prsr.add_argument("--svf-pointer-analysis", default=False, action="store_true")
    prsr.add_argument("--saved-analysis", default=None, type=Path)
    prsr.add_argument("--saved-callgraph-name", default=None, type=str)
    prsr.add_argument("--conservative", default=False, action="store_true")
    prsr.add_argument("--save-missing", default=None, type=Path)
    prsr.add_argument("--save-pts", default=None, type=Path)
    prsr.add_argument("--save-callgraph", default=None, type=Path)
    prsr.add_argument("--target-binary", default=None)

    args: Any = prsr.parse_args()

    with tempfile.TemporaryDirectory(delete=not args.save_wdir) as d:
        if args.save_wdir:
            print(f"Saving wdir {d}", file=sys.stderr)
        edges_utils.run_bracelet_edges(
            args.snapshot,
            args.pid,
            d,
            args.bracelet_edges,
            args.target_binary,
            add_call_arg_count=args.svf_pointer_analysis,
        )

        analysis: DatalogAnalysis | None = None
        flist: list[FunctionFilter] = []
        if args.run_cg_filter:
            if args.saved_analysis is not None:
                analysis = SavedAnalysis(args.saved_analysis, args.saved_callgraph_name)
            elif args.pointer_analysis and args.bracelet_clang is None:
                print(
                    "Exiting: pointer-analysis requires bracelet clang", file=sys.stderr
                )
                sys.exit(2)
            elif args.svf_pointer_analysis:
                if args.bracelet_points_to is None:
                    print(
                        "Exiting: svf-pointer-analysis requires bracelet-points-to",
                        file=sys.stderr,
                    )
                    sys.exit(2)

                target_pid = edges_utils.select_pid(
                    Path(args.snapshot), args.pid, target_bin=args.target_binary
                )
                analysis = SvfPointerAnalysis(
                    args.bracelet_points_to,
                    SVFPaths(args.svf_path, args.svf_clang, args.svf_llvm),
                    args.conservative,
                    args.snapshot,
                    target_pid,
                    args.save_missing,
                    args.save_pts,
                )
            elif args.pointer_analysis:
                analysis = PointerAnalysis(args.bracelet_edges, args.bracelet_clang)
            else:
                analysis = SimpleAnalysis()
            path_to_analyze = (
                args.saved_analysis if args.saved_analysis is not None else Path(d)
            )
            (aset, cg) = analysis.analyze_reachable_addresses(path_to_analyze)
            addr_map = get_target_address.build_sym_to_address(path_to_analyze)  # type: ignore[arg-type]
            flist.append(AddressSetFFilter(aset, cg, addr_map))
            flist.append(AdditionalFuncsFiliter(aset, addr_map))
        if args.save_callgraph is not None:
            if analysis is None:
                print(
                    "Warning: must enable cg analysis to save a callgraph",
                    file=sys.stderr,
                )
            else:
                saved_path = path_to_analyze / "output"
                output_path = Path(d) / "output"
                output_path.mkdir(exist_ok=True)
                datalog_callgraph_output.dump_callgraph(
                    saved_path,
                    CallGraph(
                        saved_path, combined_call_name=analysis.callgraph_input_name()
                    ),
                    saved_path / "EnclosingFunctionForLocal.facts",
                    output_path,
                )
                shutil.copyfile(output_path / "CallGraph.csv", args.save_callgraph)

        if args.vuln_json is None:
            print(
                "No vulnerability file provided, treating as dry run", file=sys.stderr
            )
            sys.exit(0)

        with open(args.vuln_json, "r") as f:
            vuln_j = json.load(f)

        vulns = VulnerabiliesSpec(**vuln_j)
        funcs: list[TargetFunction] = get_target_address.find_vulns(d, vulns)
        funcs_by_cve_id: defaultdict[str, list[TargetFunction]] = defaultdict(list)
        for func in funcs:
            funcs_by_cve_id[func.cve_id].append(func)

        vfilter = ManyFilters(flist)
        vuln_class: list[ReachabilityVuln] = []
        for vuln in vulns.vulnerabilities:

            if vuln.cve_id in funcs_by_cve_id:
                max_vuln_result: ReachabilityVuln | None = None
                for func in funcs_by_cve_id[vuln.cve_id]:
                    reclassified = vfilter.reclassify(vuln, func)
                    # this is a bit overengineered but we make this work for even cases where reclassification makes reachable
                    if isinstance(reclassified, ReachabilityVuln):
                        candidate = reclassified
                    else:
                        orig_justification = f"Target function {func.name} is present at version {func.version_str} at addr {func.addr}."
                        if args.run_cg_filter:
                            orig_justification += f"\n{reclassified.message}"
                        candidate = ReachabilityVuln(
                            cve_id=vuln.cve_id,
                            classification=VulnClassification.potentially_reachable,
                            justification=orig_justification,
                            address=func.addr,
                        )
                    max_vuln_result = merge_vulns(max_vuln_result, candidate)

                vuln_class.append(max_vuln_result)  # type: ignore[arg-type]
            else:
                vuln_class.append(
                    ReachabilityVuln(
                        cve_id=vuln.cve_id,
                        classification=VulnClassification.unreachable,
                        justification=f"The function {vuln.affected_function} is not present at the right version in the target",
                    )
                )

        js = ReachabilityResults(reachability_results=vuln_class).model_dump_json()
        print(js)


if __name__ == "__main__":
    main()
