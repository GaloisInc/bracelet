#!/usr/bin/env bash
set -euxo pipefail
id
getpcaps $$
# Should be invoked via core_pattern %p
DST="$COREDUMP_DST/core.$(cat /proc/$1/comm || true).$1"
/opt/ecfs/bin/ecfs -r -T -p $1 -o "$COREDUMP_DST/.tmp-core"
# Rename the coredump after it's been written so that CI won't try to upload a partial coredump.
mv "$COREDUMP_DST/.tmp-core" "$DST"

