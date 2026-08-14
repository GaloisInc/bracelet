import os
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

import jinja2
import networkx as nx
import pydot

_template_env = jinja2.Environment(
    loader=jinja2.PackageLoader("bracelet_scripts.points_to"),
    autoescape=False,
)
_makefile = _template_env.get_template("svf.Makefile")


class ContainerRuntimeNotFoundError(RuntimeError):
    pass


class Svf:
    SVF_PATH = Path(os.environ.get("SVF_PATH", "/opt/svf"))
    SVF_LLVM_PATH = Path(os.environ.get("LLVM_SVF_PATH", "/opt/svf/llvm-16.0.0.obj"))
    SVF_CLANG_PATH = Path(os.environ.get("CLANG_SVF_PATH", "/opt/svf/llvm-16.0.0.obj"))
    SVF_IMAGE = "gitlab.ebossproject.com:5005/galois/svf/svf:galois-3.1"

    def __init__(self) -> None:
        if Svf.SVF_PATH.exists():
            self.docker = None
        else:
            docker = shutil.which("podman") or shutil.which("docker")
            if not docker:
                raise ContainerRuntimeNotFoundError(
                    "We need docker or podman to invoke SVF!"
                )
            # Pull the docker image if needed.
            subprocess.check_call([docker, "run", "--rm", Svf.SVF_IMAGE, "true"])
            self.docker = docker

    def _check_call(self, work_dir: Path, args: list[str]) -> None:
        work_dir = work_dir.resolve()
        if self.docker:
            subprocess.check_call(
                [
                    self.docker,
                    "run",
                    "--rm",
                    "--workdir",
                    str(work_dir),
                    "-v",
                    f"{work_dir}:{work_dir}",
                    self.SVF_IMAGE,
                ]
                + args
            )
        else:
            subprocess.check_call(
                args,
                cwd=work_dir,
            )

    def compile_bitcode(self, src: Path) -> Path:
        """
        Compile all the *.c files in src, and then llvm-link them into a single .bc file

        Return the path to that .bc file. (The src directory's contents will be modified)
        """
        tmp = None
        try:
            # On stormbreaker, selinux configuration means that only certain directories can be
            # bind-mounted. As a result, we copy everything to a temporary directory that is
            # accessible, and then copy everything back when we're done.
            docker_tmp = os.environ.get("SVF_DOCKER_USE_TMP")
            if docker_tmp:
                tmp = tempfile.TemporaryDirectory(dir=docker_tmp)
                wd = Path(tmp.name)
                for x in src.glob("*"):
                    shutil.copy(x, wd / x.name)
            else:
                wd = src
            bc = (
                [wd / "linked.bc"]
                + [x.with_suffix(".bc") for x in wd.glob("*.cpp")]
                + [x.with_suffix(".bc") for x in wd.glob("*.c")]
            )
            for x in bc:
                if x.exists():
                    x.unlink()
            (wd / "Makefile").write_text(_makefile.render())
            self._check_call(
                wd,
                [
                    "env",
                    "SVF_CLANG=" + str(Svf.SVF_CLANG_PATH / "bin/clang"),
                    "SVF_CLANGXX=" + str(Svf.SVF_CLANG_PATH / "bin/clang++"),
                    "SVF_LLVM_LINK=" + str(Svf.SVF_LLVM_PATH / "bin/llvm-link"),
                    "make",
                    "linked.bc",
                    f"-j{os.cpu_count()}",
                ],
            )
            if docker_tmp:
                for x in bc:
                    shutil.copy(x, src / x.name)
            return src / "linked.bc"
        finally:
            if tmp:
                tmp.cleanup()

    def construct_callgraph(self, bitcode: Path) -> "nx.DiGraph[str]":
        bitcode = bitcode.resolve()
        docker_tmp = os.environ.get("SVF_DOCKER_USE_TMP")
        wd = Path(docker_tmp) if docker_tmp else bitcode.parent
        with tempfile.TemporaryDirectory(dir=wd) as tmp_name:
            tmp = Path(tmp_name)
            if docker_tmp:
                new_bitcode = tmp / "bitcode.bc"
                shutil.copy(bitcode, new_bitcode)
                bitcode = new_bitcode
            self._check_call(
                wd,
                [
                    "bash",
                    "-c",
                    "; ".join(
                        [
                            f"pushd {Svf.SVF_PATH}",
                            "source setup.sh >&2",
                            "popd",
                            "set -euxo pipefail",
                            f"cd {shlex.quote(tmp.name)}",
                            f"wpa -ander -dump-callgraph {shlex.quote(str(bitcode))}",
                        ]
                    ),
                ],
            )
            raw_dot = (tmp / "callgraph_final.dot").read_text()
            raw_graphs = pydot.graph_from_dot_data(raw_dot)
            assert raw_graphs is not None
            assert len(raw_graphs) == 1
            raw_graph = raw_graphs[0]
            node2symbol = {}
            out: nx.DiGraph[str] = nx.DiGraph()
            for n in raw_graph.get_nodes():
                symbol = n.get_label().split("fun: ")[1].split("\\")[0]  # type: ignore
                node2symbol[n.get_name()] = symbol
                out.add_node(symbol)
            out.add_edges_from(
                (
                    node2symbol[e.get_source().split(":")[0]],  # type: ignore
                    node2symbol[e.get_destination()].split(":")[0],  # type: ignore
                )
                for e in raw_graph.get_edges()
            )
            return out
