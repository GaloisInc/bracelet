import argparse
import json
import sys
from pathlib import Path
from typing import Any

from bracelet_scripts import edges_utils
from bracelet_scripts.data_format import (
    ReachabilityResults,
    ReachabilityVuln,
    VulnClassification,
)
from bracelet_scripts.screach import Screach


def attempt_analyze_vuln(screach: Screach, vuln: ReachabilityVuln) -> ReachabilityVuln:
    if vuln.address is None:
        print(
            f"Warning: a potentially reachable vuln {vuln.cve_id} does not have an address",
            file=sys.stderr,
        )
        return vuln

    vuln.screach_log = screach.run_screach(vuln.address)
    return vuln


def main() -> None:
    prsr = argparse.ArgumentParser("Screach Runner")
    prsr.add_argument("--callgraph", type=Path, required=True)
    prsr.add_argument(
        "--vulnerability-specs", required=True, type=argparse.FileType("r")
    )
    prsr.add_argument("--solver-timeout", default=5)
    prsr.add_argument("--timeout", default=30)
    prsr.add_argument("--pid", default=None, type=int)
    prsr.add_argument("snapshot", type=Path)
    args: Any = prsr.parse_args()

    selected_pid = edges_utils.select_pid(args.snapshot, args.pid)

    vulns = ReachabilityResults(**json.load(args.vulnerability_specs))
    scrch = Screach(
        str(args.callgraph),
        args.timeout,
        args.solver_timeout,
        str(args.snapshot / f"{selected_pid}.ecfs.core"),
    )

    new_results: list[ReachabilityVuln] = []

    # TODO there is no reason to do this in serial
    for vuln in vulns.reachability_results:
        if (
            vuln.classification == VulnClassification.unreachable
            or vuln.classification == VulnClassification.unable_to_assess
        ):
            new_results.append(vuln)
        else:
            new_results.append(attempt_analyze_vuln(scrch, vuln))

    js = ReachabilityResults(reachability_results=new_results).model_dump_json()
    print(js)


if __name__ == "__main__":
    main()
