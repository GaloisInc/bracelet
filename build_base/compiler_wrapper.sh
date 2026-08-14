#!/usr/bin/env bash
set -eo pipefail

compiler=${@compiler_override@:-@compiler@}

for arg in "$@"; do
    if [[ "$arg" == *.s ]] || [[ "$arg" == *.S ]]; then
        # We don't pass extra args for assembly
        args_array=()
        for arg in "$@"; do
            if [[ "$arg" != -mllvm=--bracelet-sbom* ]] && [[ "$arg" != -mllvm=--no-bracelet-include* ]]; then
                    args_array+=("$arg")
            fi
        done
        exec "$compiler" "${args_array[@]}"
    fi
done
BASE="$(dirname "$0")"
BRACELET_FILE_TABLE=$(python3 "$BASE/build_file_table.py" "$@")


declare -a launcher=()
if [[ -f "$BASE/sccache-setup.sh" ]]; then
   source "$BASE/sccache-setup.sh"
fi

declare -a prefix_flags

BRACELET_TRACE="${BRACELET_TRACE:-@bracelet_trace@}"
if [[ "$BRACELET_TRACE" != "off" ]]; then
    prefix_flags+=(
        "-mllvm=--bracelet-trace=$BRACELET_TRACE"
	"-L/opt/bracelet-llvm/bin"
	"-lbracelet_pointsto_trace_runtime"
    )
fi

BRACELET_PROFILE="${BRACELET_PROFILE:-@bracelet_profile@}"
if [[ "$BRACELET_PROFILE" != "off" ]]; then
    launcher=(valgrind --tool=callgrind)
fi
braceletreach=$(realpath "$BASE/libbracelet_reachability.so")

# SCCACHE_EXTRAFILES is required because sccache does not correctly parse
# fplugin=..
SCCACHE_EXTRAFILES=$braceletreach exec ${launcher[@]} "$compiler" \
    ${prefix_flags[@]} \
    "$@" \
    -g3 \
    "-fplugin=$braceletreach" \
    "-fpass-plugin=$braceletreach" \
    "-mllvm=-emit-jump-table-sizes-section" \
    "-mllvm=--bracelet-emit-edges" \
    -mllvm --bracelet-file-table="${BRACELET_FILE_TABLE}" \
    "-mllvm=--bracelet-dlsym-runtime=$(realpath "$BASE/dlsym_runtime.bc")" \
    -Wl,--emit-relocs \
    @extra_flags@ \
    -Wno-error
