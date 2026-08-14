import argparse
import csv
import hashlib
import itertools
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

# LLDB import
import lldb  # type: ignore
import networkx as nx

# SARIF imports
from sarif_pydantic.sarif import (  # type: ignore
    ArtifactLocation,
    CodeFlow,
    Location,
    Message,
    PhysicalLocation,
    Region,
    Result,
    Run,
    Sarif,
    ThreadFlow,
    ThreadFlowLocation,
    Tool,
    ToolDriver,
)


@dataclass
class LineLoc:
    file: str
    start_line: int
    start_col: int
    end_line: int
    end_col: int


def setup_target(
    snap_path: str, bin_path: str, core_path: str
) -> tuple[lldb.SBDebugger, lldb.SBTarget]:
    debugger = lldb.SBDebugger().Create()
    debugger.SetCurrentPlatformSDKRoot(snap_path)
    error = lldb.SBError()
    target = debugger.CreateTarget(bin_path, None, None, True, error)

    if error.Fail():
        print(f"Target Creation Error: {error}", file=sys.stderr)

    core_load = target.LoadCore(core_path)
    if not core_load:
        print(f"Core Load Error: Failed to load core file {core_path}", file=sys.stderr)

    lldb.target = target
    return (debugger, target)


def apply_prefix_mapping(path: str, prefix_map: dict[str, str]) -> str:
    for old_prefix, new_prefix in prefix_map.items():
        if path.startswith(old_prefix):
            return path.replace(old_prefix, new_prefix, 1)
    return path


def addresses_to_line_locs(
    addresses: list[int],
    target: lldb.SBTarget,
    prefix_map: dict[str, str] | None = None,
    source_target: Path | None = None,
) -> dict[int, tuple[str, LineLoc]]:
    """
    Given a list of addresses, resolve them to LineLocs using LLDB.
    Converts paths to be relative to source_target if they are inside it.
    """
    if prefix_map is None:
        prefix_map = {}

    addr_to_line_loc: dict[int, tuple[str, LineLoc]] = {}

    if source_target:
        source_target = source_target.absolute()

    for addr in addresses:
        sb_addr = target.ResolveLoadAddress(addr)
        line_entry = sb_addr.line_entry

        if line_entry and line_entry.file:
            file_path = line_entry.file.fullpath
            mapped_path = apply_prefix_mapping(file_path, prefix_map)

            # Make path relative if it's inside the source_target
            if source_target:
                try:
                    mapped_path_obj = Path(mapped_path).absolute()
                    mapped_path = str(mapped_path_obj.relative_to(source_target))
                except ValueError:
                    # Path is not inside source_target, leave it as is
                    pass

            start_line = line_entry.line
            start_col = line_entry.column if line_entry.column else 1

            fn_name = sb_addr.function.name if sb_addr.function else "unknown_function"

            # Single line locations without tree-sitter
            addr_to_line_loc[addr] = (
                fn_name,
                LineLoc(
                    file=mapped_path,
                    start_line=start_line,
                    start_col=start_col,
                    end_line=start_line,
                    end_col=start_col,
                ),
            )

    return addr_to_line_loc


def parse_node(val: str) -> int | str:
    """Safely converts hex strings to ints, leaving string symbols intact."""
    val = val.strip()
    try:
        return int(val, 16)
    except ValueError:
        return val


def build_callgraph(csv_path: Path, target: lldb.SBTarget) -> "nx.DiGraph[Any]":
    """
    Builds a NetworkX Directed Graph from the callgraph, bridging
    library boundaries by injecting zero-weight symbol alias edges.
    """
    G: nx.DiGraph[Any] = nx.DiGraph()
    with open(csv_path, "r", encoding="utf-8") as f:
        reader = csv.reader(f, delimiter="\t")
        for row in reader:
            if len(row) >= 3:
                caller_func = parse_node(row[0])
                caller_addr = parse_node(row[1])
                callee_func = parse_node(row[2])

                # Add edge: Caller Function -> Callee Function (forward direction)
                G.add_edge(caller_func, callee_func, call_site=caller_addr)

    # Enhance graph with LLDB bridging edges
    nodes = list(G.nodes)
    for node in nodes:
        if isinstance(node, int):
            sb_addr = target.ResolveLoadAddress(node)

            start_addr = None
            if sb_addr.function:
                start_addr = sb_addr.function.GetStartAddress().GetLoadAddress(target)
            elif sb_addr.symbol:
                start_addr = sb_addr.symbol.GetStartAddress().GetLoadAddress(target)

            if (
                start_addr
                and start_addr != lldb.LLDB_INVALID_ADDRESS
                and start_addr != node
            ):
                G.add_edge(start_addr, node, call_site=start_addr)

            sym_name = None
            if sb_addr.symbol and sb_addr.symbol.name:
                sym_name = sb_addr.symbol.name
            elif sb_addr.function and sb_addr.function.name:
                sym_name = sb_addr.function.name

            if sym_name:
                G.add_edge(sym_name, node, call_site=sym_name)
                if start_addr and start_addr != lldb.LLDB_INVALID_ADDRESS:
                    G.add_edge(sym_name, start_addr, call_site=sym_name)

    return G


def find_path_to_target(
    G: "nx.DiGraph[Any]", target_addr: int, target: lldb.SBTarget, bin_path: str
) -> list[int]:
    """
    Finds the shortest path from 'main' to the target address using NetworkX.
    Falls back to the furthest root if 'main' is not an ancestor.
    """
    search_target: int | str = target_addr

    # If the target *instruction* isn't in the graph, find the *function* it belongs to
    if search_target not in G:
        sb_addr = target.ResolveLoadAddress(target_addr)
        func_start = None
        if sb_addr.function:
            func_start = sb_addr.function.GetStartAddress().GetLoadAddress(target)
        elif sb_addr.symbol:
            func_start = sb_addr.symbol.GetStartAddress().GetLoadAddress(target)

        if func_start and func_start in G:
            search_target = func_start
        else:
            return [target_addr]  # Cannot trace

    ancestors = nx.ancestors(G, search_target)
    main_node: int | str | None = None

    # 1. Identify which ancestor is 'main'
    for anc in ancestors:
        if isinstance(anc, int):
            sb_addr = target.ResolveLoadAddress(anc)
            sym_name = None
            if sb_addr.function and sb_addr.function.name:
                sym_name = sb_addr.function.name
            elif sb_addr.symbol and sb_addr.symbol.name:
                sym_name = sb_addr.symbol.name

            if sym_name == "main":
                main_node = anc
                break

    # 2. Check if the string alias "main" itself is an ancestor
    if main_node is None and "main" in ancestors:
        main_node = "main"

    # 3. Pathfinding via NetworkX
    func_path: list[int | str] = []
    if main_node is not None:
        try:
            func_path = nx.shortest_path(G, source=main_node, target=search_target)
        except nx.NetworkXNoPath:
            pass

    # 4. Fallback: Find the furthest root ancestor (in-degree 0)
    if not func_path:
        roots = [n for n in ancestors if G.in_degree(n) == 0]
        if roots:
            longest_path: list[int | str] = []
            for r in roots:
                try:
                    p = nx.shortest_path(G, source=r, target=search_target)
                    if len(p) > len(longest_path):
                        longest_path = p
                except nx.NetworkXNoPath:
                    continue
            func_path = longest_path

    if not func_path:
        return [target_addr]

    # 5. Extract executable instruction addresses from the sequence of functions
    trace: list[int] = []

    def add_to_trace(addr: Any) -> None:
        if isinstance(addr, int) and (not trace or trace[-1] != addr):
            trace.append(addr)

    for u, v in itertools.pairwise(func_path):
        if isinstance(u, int):
            add_to_trace(u)
        elif isinstance(u, str):
            sc_list = target.FindFunctions(u)
            if sc_list.GetSize() > 0:
                ctx = sc_list.GetContextAtIndex(0)
                if ctx.function:
                    add_to_trace(ctx.function.GetStartAddress().GetLoadAddress(target))
                elif ctx.symbol:
                    add_to_trace(ctx.symbol.GetStartAddress().GetLoadAddress(target))

        edge_data = G.get_edge_data(u, v)
        call_site = edge_data.get("call_site")
        if isinstance(call_site, int):
            add_to_trace(call_site)

    if func_path:
        last_node = func_path[-1]
        if isinstance(last_node, int):
            add_to_trace(last_node)

    add_to_trace(target_addr)
    return trace


def create_sarif_result(
    path_addresses: list[int],
    locs_map: dict[int, tuple[str, LineLoc]],
    cve_id: str,
    classification: str,
    target: str,
    justification: str,
    source_target: Path | None = None,
) -> Result | None:
    """
    Creates a SARIF Result object for a single traced address.
    Attaches the closest in-target location as the top-level Result location.
    """
    thread_flow_locations: list[ThreadFlowLocation] = []
    top_level_location: Location | None = None

    # 1. Identify the closest in-target execution step to serve as the top-level location
    target_idx = -1
    total_steps = len(path_addresses)
    for idx in range(total_steps - 1, -1, -1):
        addr = path_addresses[idx]
        loc_data = locs_map.get(addr)
        if not loc_data:
            continue
        loc = loc_data[1]
        # Drop absolute paths if we are filtering by source_target
        if source_target and Path(loc.file).is_absolute():
            continue
        target_idx = idx
        break

    # 2. Fallback: If no in-target location was found, use the last available location in the trace
    if target_idx == -1:
        for idx in range(total_steps - 1, -1, -1):
            if locs_map.get(path_addresses[idx]):
                target_idx = idx
                break

    # 3. Build the ThreadFlows and capture the top-level location
    level = 1
    for idx, addr in enumerate(path_addresses):
        # Skip caller function steps
        if idx % 2 == 0 and idx != (len(path_addresses) - 1):
            continue

        loc_data = locs_map.get(addr)
        if not loc_data:
            continue

        fn_name = loc_data[0]
        loc = loc_data[1]

        region = Region(
            startLine=loc.start_line,
            startColumn=loc.start_col,
            endLine=loc.end_line,
            endColumn=loc.end_col,
        )

        uri = f"file://{loc.file}" if Path(loc.file).is_absolute() else loc.file

        physical_loc = PhysicalLocation(
            artifactLocation=ArtifactLocation(uri=uri), region=region
        )

        message = (
            Message(text=f"Vulnerable function {fn_name} ({hex(addr)})")
            if idx == len(path_addresses) - 1
            else Message(text=f"Call site in {fn_name} ({hex(addr)})")
        )

        location_obj = Location(physicalLocation=physical_loc, message=message)

        # Save this location if it matches our chosen target_idx
        if idx == target_idx:
            top_level_location = location_obj

        tf_loc = ThreadFlowLocation(location=location_obj, nestingLevel=level)
        level += 1
        thread_flow_locations.append(tf_loc)

    if not thread_flow_locations:
        return None

    code_flow = CodeFlow(threadFlows=[ThreadFlow(locations=thread_flow_locations)])

    # Bundle the vulnerability metadata into the SARIF message
    full_message = f"[{cve_id}] {classification.upper()}\n{justification}."

    return Result(
        ruleId=cve_id,
        message=Message(text=full_message),
        locations=[top_level_location] if top_level_location else None,
        codeFlows=[code_flow],
    )


def create_codequality_issue(
    path_addresses: list[int],
    locs_map: dict[int, tuple[str, LineLoc]],
    cve_id: str,
    classification: str,
    target: str,
    justification: str,
    source_target: Path | None = None,
) -> dict[str, Any] | None:
    """
    Creates a GitLab Code Quality issue dictionary for the closest execution step.
    """
    total_steps = len(path_addresses)

    for idx in range(total_steps - 1, -1, -1):
        addr = path_addresses[idx]
        loc_data = locs_map.get(addr)
        if not loc_data:
            continue

        fn_name = loc_data[0]
        loc = loc_data[1]

        if source_target and Path(loc.file).is_absolute():
            continue

        fingerprint = hashlib.md5(
            f"trace-step-{cve_id}-{addr}-{idx}".encode()
        ).hexdigest()

        is_target = idx == total_steps - 1
        step_desc = (
            f"Vulnerable function {target} reachable at address {hex(addr)}"
            if is_target
            else f"Vulnerable function {target} reachable from call site in {fn_name} at address {hex(addr)}"
        )

        # Bundle the vulnerability metadata into the CodeQuality description
        full_description = (
            f"[{cve_id}] {classification.upper()} - {step_desc}. {justification}."
        )

        issue = {
            "description": full_description,
            "fingerprint": fingerprint,
            "severity": "critical",
            "location": {
                "path": loc.file,
                "lines": {"begin": loc.start_line, "end": loc.end_line},
            },
        }
        return issue

    return None


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate SARIF and CodeQuality traces from reachability results."
    )
    parser.add_argument(
        "--snap-dir",
        required=True,
        type=Path,
        help="Snapshot directory containing bin and core",
    )
    parser.add_argument(
        "--callgraph", required=True, type=Path, help="Path to callgraph.csv"
    )
    parser.add_argument(
        "--results-json",
        required=True,
        type=Path,
        help="Path to the JSON file containing reachability results",
    )
    parser.add_argument(
        "--source-target",
        required=False,
        type=Path,
        help="Root directory to make source paths relative to",
    )
    parser.add_argument(
        "--out-sarif", default="output.sarif", type=Path, help="Output SARIF file path"
    )
    parser.add_argument(
        "--out-cq",
        default="gl-codequality.json",
        type=Path,
        help="Output GitLab CodeQuality JSON file path",
    )
    args = parser.parse_args()

    # 1. Automatically locate the binary and core pairs, ignoring .ecfs.core
    exe_files = list(args.snap_dir.rglob("*.exe"))
    valid_pairs: list[tuple[str, str]] = []

    for exe in exe_files:
        core_candidate = exe.with_suffix(".core")
        if core_candidate.exists() and not core_candidate.name.endswith(".ecfs.core"):
            valid_pairs.append((str(exe), str(core_candidate)))

    if not valid_pairs:
        print(
            f"Error: Could not find any matching .exe and .core pairs in {args.snap_dir}",
            file=sys.stderr,
        )
        sys.exit(1)

    # 2. Load JSON Reachability Results (Only need to load once)
    with open(args.results_json, "r", encoding="utf-8") as f:
        data = json.load(f)

    reachability_results = data.get("reachability_results", [])
    print(f"Loaded {len(reachability_results)} targets from JSON.")

    sarif_results_list: list[Result] = []
    cq_issues_list: list[dict[str, Any]] = []

    # Track fingerprints to prevent duplicate reports if the exact same issue is found in multiple cores
    seen_cq_fingerprints: set[str] = set()
    seen_sarif_traces: set[str] = set()

    # 3. Process each valid Executable and Core pair
    for bin_path, core_path in valid_pairs:
        print("\n========================================")
        print("Processing Pair:")
        print(f"  Executable: {bin_path}")
        print(f"  Core File:  {core_path}")
        print("========================================")

        print("Setting up LLDB target...")
        _debugger, target = setup_target(str(args.snap_dir), bin_path, core_path)

        print(f"Parsing call graph from {args.callgraph}...")
        G = build_callgraph(args.callgraph, target)

        # 4. Iterate through EACH target in the JSON for the current Pair
        for item in reachability_results:
            addr_str = item.get("address")
            if not addr_str:
                # Silently skip items with no address
                continue

            target_addr = int(addr_str, 16)
            cve_id = item.get("cve_id", "Unknown CVE")
            classification = item.get("classification", "Unknown Classification")
            justification = item.get("justification", "No justification provided.")

            target_fn_match = re.search(
                r"Target function (.*?) is present", justification
            )
            target_fn = (
                target_fn_match.group(1) if target_fn_match else "Unknown Target"
            )

            justification = re.sub(
                r"Target function", "Vulnerable function", justification
            )
            justification = re.sub(
                r"No additional symbols were required to be reachable.",
                "",
                justification,
            )
            justification = justification.strip()

            print(f"  > Processing {cve_id} at {addr_str}...")

            # Trace Path
            path = find_path_to_target(G, target_addr, target, bin_path)
            print(f"    Found path of length {len(path)}")

            # Resolve Lines
            locs_map = addresses_to_line_locs(
                path, target, prefix_map=None, source_target=args.source_target
            )

            # Accumulate SARIF
            sarif_res = create_sarif_result(
                path,
                locs_map,
                cve_id,
                classification,
                target_fn,
                justification,
                args.source_target,
            )

            if sarif_res:
                # Deduplicate SARIF results
                trace_key = f"{cve_id}-" + "-".join(str(a) for a in path)
                if trace_key not in seen_sarif_traces:
                    seen_sarif_traces.add(trace_key)
                    sarif_results_list.append(sarif_res)

            # Accumulate CodeQuality
            cq_issue = create_codequality_issue(
                path,
                locs_map,
                cve_id,
                classification,
                target_fn,
                justification,
                args.source_target,
            )

            if cq_issue:
                # Deduplicate CodeQuality results
                fp = cq_issue["fingerprint"]
                if fp not in seen_cq_fingerprints:
                    seen_cq_fingerprints.add(fp)
                    cq_issues_list.append(cq_issue)

    # 5. Generate combined SARIF output
    print(f"\nGenerating combined SARIF report to {args.out_sarif}...")
    run = Run(tool=Tool(driver=ToolDriver(name="Bracelet")), results=sarif_results_list)
    sarif_log = Sarif(
        version="2.1.0",
        schema="https://json.schemastore.org/sarif-2.1.0.json",
        runs=[run],
    )
    with open(args.out_sarif, "w") as f:
        f.write(sarif_log.model_dump_json(indent=2, by_alias=True, exclude_none=True))

    # 6. Generate combined CodeQuality output
    print(f"Generating combined GitLab CodeQuality report to {args.out_cq}...")
    with open(args.out_cq, "w") as f:
        json.dump(cq_issues_list, f, indent=2)

    print("Done.")


if __name__ == "__main__":
    main()
