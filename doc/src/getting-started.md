# Getting Started


## Installing

The BRACELET toolchain provides a nix-shell that builds a modified clang, LLVM pass, compiler wrapper, and VCPKG toolchain.

The shell sets VCPKG_OVERLAY_TRIPLETS so that the bracelet triplet is available by default

First you need to install [nix](https://nix.dev/install-nix.html)

The build depends on [nix-ccache](https://nixos.wiki/wiki/CCache) to enable fast recompilation of LLVM. This can be setup by exposing the ccache dir:
```sh
sudo mkdir -m0770 -p /nix/var/cache/ccache
sudo chown --reference=/nix/store /nix/var/cache/ccache
```

The shared cache is also reused when a Nix build is interrupted and restarted.
The first LLVM build still takes substantially longer than later builds.

The directory needs to be added to the extra sandbox in the nix.conf (typically `$HOME/.config/nix/nix.conf`):
```
extra-sandbox-paths = /nix/var/cache/ccache
```

Unfortunately, the build of BRACELET is not currently completely isolated so you will have to add the following setting as well:
```
sandbox = relaxed
```

Finally, if you receive a warning:
```
warning: ignoring the user-specified setting 'extra-sandbox-paths', because it is a restricted setting and you are not a trusted user
```
When you execute nix commands (e.g. nix-build/nix-shell) you will need to add the following:
```
trusted-users = <username>
```
to /etc/nix/nix.conf (for a multi-user install).

To apply this setting you will need to restart the nix-daemon:
```sh
sudo systemctl daemon-reload
sudo systemctl restart nix-daemon
```


From the repository root, enter the development environment:

```bash
nix-shell
```

This builds LLVM and BRACELET, creates and activates a Python virtual
environment, and sets the toolchain environment:

- `VCPKG_OVERLAY_TRIPLETS` makes the BRACELET vcpkg triplets available.
- `BRACELET_TOOLCHAIN_FILE` selects the BRACELET compiler wrappers.
- `VCPKG_TOOLCHAIN_FILE` selects vcpkg's CMake integration.
- `BRACELET_INCLUDE_DIR` provides the snapshot API headers.
- `SVF_PATH`, `SVF_CLANG_PATH`, and `SVF_LLVM_PATH` configure optional SVF
  analysis.

Inside the environment you should be able to execute `bracelet-cc.sh`

## Building With Upstream LLVM

The pass can also be compiled and loaded with an upstream LLVM 20 installation:

```bash
env LLVM_CONFIG=<path to upstream llvm-config> uv run --dev meson setup build-upstream \
  -Dbracelet_llvm_extensions=false \
  -Dclang-dir=<path to upstream clang bin directory>
uv run --dev meson compile -C build-upstream bracelet_reachability
```

This mode omits the fork-specific metadata that places `DW_TAG_label` entries
at callsite addresses. It is useful for CI, but its output is not suitable for
BRACELET callsite analysis. CI also compiles a sample program and verifies that
the pass emits decodable `GR_graph_edges` and `GR_graph_debug` sections.

This build does not exercise SVF. The separate `bracelet-points-to` tool invokes
an SVF installation or container at runtime.

## Building a Project 

The [Example 1 walkthrough](./example1.md) describes how to build a project and run the reachability analysis.
