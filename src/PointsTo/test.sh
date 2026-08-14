#!/usr/bin/env bash
# Invoke this program like: ./test.sh one.c
# where one.c is one of the sample test_programs/
set -euxo pipefail
cd "$(dirname "$0")"/../../
ROOT="$PWD"
BRACELET_TRACE="${BRACELET_TRACE:-full}"
COMPILER="${COMPILER:-bracelet-cc.sh}"
TMP="/tmp/work-dir-${1}-${BRACELET_TRACE}"
rm -rf "$TMP"
mkdir -p "$TMP"
cd "$TMP"
export BRACELET_TRACE="${BRACELET_TRACE}"
export BRACELET_TRACE_DIR=$PWD/traces
mkdir -p $BRACELET_TRACE_DIR
RUNTIME_ARGS="-Wl,-rpath=${ROOT}/build -L${ROOT}/build -lbracelet_pointsto_trace_runtime"
$ROOT/build/$COMPILER -o test.so -shared $RUNTIME_ARGS $ROOT/src/PointsTo/test_programs/test_so.c
$ROOT/build/$COMPILER -o test.exe $RUNTIME_ARGS $ROOT/src/PointsTo/test_programs/$1
$ROOT/build/$COMPILER -o test.ll -S -emit-llvm $RUNTIME_ARGS $ROOT/src/PointsTo/test_programs/$1
./test.exe
$ROOT/build/bracelet-points-to --tmp $PWD test.exe --sysroot / --core core.* --trace-dir $BRACELET_TRACE_DIR
