#!/usr/bin/env bash
set -euxo pipefail
id
export COREDUMP_DST=$(cat /opt/bracelet-llvm/coredump-dst.txt)
mkdir -p "$COREDUMP_DST"
/opt/bracelet-llvm/coredump.sh "$@" 2>&1 | tee "$COREDUMP_DST/.stdout.txt"

