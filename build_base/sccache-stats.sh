#!/usr/bin/env bash
set -euxo pipefail
source "$(dirname "$0")/sccache-setup.sh"
/opt/galois-llvm-build/bin/sccache --show-stats
