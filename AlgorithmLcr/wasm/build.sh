#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
if ! command -v emcmake >/dev/null; then
  sdk=${EMSDK:-$HOME/emsdk}
  source "$sdk/emsdk_env.sh"
fi
build=${LCR_WASM_BUILD:-/tmp/lcr-v4-wasm}
emcmake cmake -S "$root/AlgorithmLcr" -B "$build" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "$build" --target lcr_wasm -j2
mkdir -p "$root/frontend/src/wasm"
cp "$build/lcr.js" "$build/lcr.wasm" "$root/frontend/src/wasm/"
