# Overview


BRACELET (Binary Reachability Analysis with Compiler-Enhanced Lifting for
Execution and Triage) is a set of tools for triaging vulnerabilities in the
dependencies of a C or C++ application. BRACELET assists security engineers
when triaging upstream vulnerability reports by automatically excluding
vulnerabilities that are unreachable in their specific application context.

![BRACELET workflow](bracelet.png)

BRACELET currently supports C and C++ applications built with [vcpkg].[^vcpkg]
The first step to using BRACELET is to compile the application and its
dependencies with the BRACELET toolchain. This toolchain embeds lightweight
metadata into the final enhanced application binary. This metadata later feeds
into BRACELET's analyses.

[vcpkg]: https://vcpkg.io/en/
[^vcpkg]: BRACELET is not *fundamentally* tied to vcpkg, and with a bit of work could be used with other build systems.

When a CVE is discovered in a dependency, a developer or user encodes
information about it (e.g., the affected package name, version, and function)
into [a simple, small JSON file](vulns.md). BRACELET consumes this information
and analyzes the shipped enhanced binary to determine whether the vulnerability
is reachable.

At a high level, BRACELET performs several tiers of analyses of escalating
complexity and power:

1. BRACELET first checks that the application was linked against the dependency
   version(s) that are affected by the vulnerability.
2. Next, it performs a form of precise yet scalable static analysis (pointer
   analysis) to construct a sound (i.e., overapproximate) and precise
   callgraph. If the vulnerable function is not reachable from `main`, the
   vulnerability is reported as unreachable.
3. If the vulnerability is deemed reachable, BRACELET supports synthesis of a
   test-case with [Screach], a tool for binary-level symbolic execution.

The first two steps are highly automated. The third generally requires some
manual intervention.

[Screach]: https://github.com/GaloisInc/grease/tree/main/screach

Notably, BRACELET's triaging capabilities do *not* require access to source
code. If an application was built the BRACELET toolchain, downstream users may
use BRACELET to triage even a closed-source application.

The following diagram shows the BRACELET workflow in a bit more detail:

```mermaid
---
title: BRACELET Workflow
---
flowchart 
direction TB
    classDef artifact fill:#d9d5d4;
    classDef braceletImpl fill:#c2e4ff;
    classDef upstream fill:#cea6f5
    subgraph Building
        vcpkg[VCPKG Application]
        vcpkg_port1[VCPKG Port 1]
        vcpkg_port2[VCPKG Port 2]
        bracelet_compiler[BRACELET Extended Compiler]
        app[Compiled Application]
        port_lib1[Port 1 Library Artifact]
        port_lib2[Port 2 Library Artifact]


        vcpkg --Package Metadata--> bracelet_compiler
        vcpkg_port1 --Port Metadata--> bracelet_compiler
        vcpkg_port2 --Port Metadata--> bracelet_compiler

        bracelet_compiler --> app
        bracelet_compiler --> port_lib1
        bracelet_compiler --> port_lib2

        class vcpkg,vcpkg_port1,vcpkg_port2,app,port_lib1,port_lib2 artifact;
        class bracelet_compiler braceletImpl;
    end
    app ----> running
    port_lib1 ----> running
    port_lib2 ----> running
    subgraph Snapshotting
        running[Running Program]
        snapshot_script[Snapshot Script]
        snapshot[Snapshot]

        running --> snapshot_script
        snapshot_script --> snapshot

        class snapshot_script braceletImpl;
        class running,snapshot artifact;
    end
    subgraph Reachability
        snapshot[Snapshot]
        vuln[Vulnerabilities JSON]
        bracelet-entry[BRACELET Entrypoint Script]
        reachability-results[Reachability Results JSON]
        snapshot --> bracelet-entry
        vuln --> bracelet-entry
        bracelet-entry --"Points-to analysis"--> reachability-results
        class snapshot,vuln,reachability-results artifact;
        class bracelet-entry-post,points-to,bracelet-entry,bracelet-edges,lightweight-analysis braceletImpl;
        class svf upstream;
    end
```

The BRACELET entrypoint script consumes a snapshot and vulnerability description
and then orchestrates a reachability analysis. The script extracts metadata
using bracelet-edges to determine the address of function entrypoints associated
with each vulnerability. This matching checks for the existence of a target
symbol and that it came from the vulnerable version range specified in the
vulnerability.json. The script then constructs a callgraph either via a
lightweight datalog analysis or SVF pointer analysis. If the target function
is not in the callgraph the vulnerability is marked unreachable, otherwise the
function is labeled as potentially reachable and a sample callgraph path is
included as evidence.

Below is a detailed overview relating analysis steps to implementation details. For getting started read [this chapter](./getting-started.md) for installation instructions and the [Example 1 walkthrough](./example1.md) for a complete example.

## Detailed Overview

The following diagram expands on the one shown above with additional details:

```mermaid
---
title: BRACELET Workflow
---
flowchart 
direction TB
    classDef artifact fill:#d9d5d4;
    classDef braceletImpl fill:#c2e4ff;
    classDef upstream fill:#cea6f5
    subgraph Building
        vcpkg[VCPKG Application]
        vcpkg_port1[VCPKG Port 1]
        vcpkg_port2[VCPKG Port 2]
        bracelet_compiler[BRACELET Extended Compiler]
        app[Compiled Application]
        port_lib1[Port 1 Library Artifact]
        port_lib2[Port 2 Library Artifact]


        vcpkg --Package Metadata--> bracelet_compiler
        vcpkg_port1 --Port Metadata--> bracelet_compiler
        vcpkg_port2 --Port Metadata--> bracelet_compiler

        bracelet_compiler --> app
        bracelet_compiler --> port_lib1
        bracelet_compiler --> port_lib2

        class vcpkg,vcpkg_port1,vcpkg_port2,app,port_lib1,port_lib2 artifact;
        class bracelet_compiler braceletImpl;
    end
    Building --"Post-hoc Triage"--> Snapshotting
    subgraph Snapshotting
        direction LR
        shared_lib[Shared Libraries]
        dyn_lib[Dynamic Libraries]
        app2[Application]
        loader[Linker/Loader]
        running[Running Program]
        snapshot_script[Snapshot Script]
        gdb[GDB]
        coredump[Coredump]
        sysroot[Sysroot]

        app2 --> loader
        shared_lib --> loader
        loader --> running
        dyn_lib --"Linked at Runtime"--> running

        running --> snapshot_script
        snapshot_script --> gdb
        gdb --> coredump
        gdb --"Shared Libraries"--> sysroot

        class snapshot_script braceletImpl;
        class gdb,loader upstream;
        class app2,dyn_lib,shared_lib,running,coredump,sysroot artifact;
    end
    Snapshotting --"Reachability Analysis"--> Reachability
    subgraph Reachability
        direction LR
        snapshot[Snapshot]
        vuln[Vulnerabilities JSON]
        bracelet-entry[BRACELET Entrypoint Script]
        bracelet-entry-post[BRACELET Entrypoint Post Processing]
        svf[SVF]
        bracelet-edges[BRACELET Edges]
        lightweight-analysis[Lightweight Datalog Analysis]
        points-to[BRACELET Points-To]
        reachability-results[Reachability Results JSON]
        snapshot --> bracelet-entry
        vuln --> bracelet-entry
        bracelet-entry --> bracelet-edges
        bracelet-edges --"Datalog Facts"--> lightweight-analysis
        lightweight-analysis --"Callgraph"--> bracelet-entry-post
        bracelet-entry --> points-to
        points-to --"C Representation of Edges"--> svf
        svf --Callgraph--> bracelet-entry-post
        bracelet-entry-post --> reachability-results
        class snapshot,vuln,reachability-results artifact;
        class bracelet-entry-post,points-to,bracelet-entry,bracelet-edges,lightweight-analysis braceletImpl;
        class svf upstream;
    end
```

And the compilation workflow:

```mermaid
---
title: BRACELET Compilation Workflow
---
flowchart LR
    file[Source File]
    clang[Clang]
    comp_wrap[BRACELET Compiler Wrapper]
    reachability_pass[Reachability Pass]
    annotated_binary[Annotated Object File]

    reachability_pass --"Invoked by"--> comp_wrap
    comp_wrap --"Appends required flags"---> clang
    file --> comp_wrap
    clang --> annotated_binary

    classDef artifact fill:#d9d5d4;
    classDef braceletImpl fill:#c2e4ff;

    class comp_wrap,reachability_pass braceletImpl;
    class file,annotated_binary artifact;
```

The above graph shows how a single source file is compiled with points-to metadata. The BRACELET toolchain compiler wrapper generated from `build_base/compiler_wrapper.sh`
consumes a source file and produces an object file containing metadata in the `GR_graph_edges` and `GR_graph_debug` sections. The rest of the compilation process after 


The reachability LLVM pass located in 
`src/BRACELETReachability` traverses an LLVM module and creates globals in several sections that store tuples described in the [metadata](metadata.md) chapter.
