#!/usr/bin/env bash
set -euo pipefail

bracelet_cc=$(realpath "$1")
bracelet_edges=$(realpath "$2")
source_file=$(realpath "$3")

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

"$bracelet_cc" -O1 -o "$tmp/smoke" "$source_file"

readelf=$(command -v llvm-readelf || command -v readelf)
sections=$("$readelf" --sections "$tmp/smoke")
grep -w GR_graph_debug <<<"$sections" >/dev/null
grep -w GR_graph_edges <<<"$sections" >/dev/null

mkdir "$tmp/facts"
(
  cd "$tmp/facts"
  "$bracelet_edges" ../smoke
)

main_dir=
for function_name in "$tmp"/facts/0x*/function-name.txt; do
  if grep -qx main "$function_name"; then
    main_dir=$(dirname "$function_name")
    break
  fi
done

test -n "$main_dir"
test -s "$main_dir/Call.facts"
test -s "$main_dir/DebugTable.facts"
