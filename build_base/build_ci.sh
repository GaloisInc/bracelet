#!/usr/bin/env bash
set -euxo pipefail
cd "$(dirname "$0")"/..
export LLVM_CONFIG=/opt/galois-llvm-build/bin/llvm-config
declare -a launcher=()
source build_base/sccache-setup.sh
if [[ ${#launcher[@]} -eq 1 ]]; then
  PATH_NEW=$(mktemp -d)
  ln -s /opt/galois-llvm-build/bin/sccache "$PATH_NEW/sccache"
  export PATH="$PATH_NEW:$PATH"
  sccache --version
else
  echo "Sccache disabled"
fi
uv run --dev meson setup build \
  --prefix=/opt/bracelet-llvm \
  --buildtype=debugoptimized \
  $((test -f ci-flags.txt && cat ci-flags.txt) || true)
uv run --dev ninja -C build test
uv run --dev ninja -C build install
cp ./build_base/sccache-setup.sh /opt/bracelet-llvm/bin
cp ./build_base/sccache-stats.sh /opt/bracelet-llvm/bin
./build_base/sccache-stats.sh
