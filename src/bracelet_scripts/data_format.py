from enum import Enum

from pydantic import BaseModel


class VulnClassification(Enum):
    reachable = "reachable"
    unreachable = "unreachable"
    potentially_reachable = "potentially reachable"
    unable_to_assess = "unable to assess"


class ReachabilityVuln(BaseModel):
    cve_id: str
    classification: VulnClassification
    justification: str
    address: str | None = None
    screach_log: str | None = None


class ReachabilityResults(BaseModel):
    reachability_results: list[ReachabilityVuln]
