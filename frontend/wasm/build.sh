#!/usr/bin/env bash
# Build the lcr_wasm WebAssembly module from the AlgorithmLcr C++ engines and
# copy the artifacts into frontend/src/wasm/ (committed to git — see AGENTS.md).
#
# Prereq (once): git clone https://github.com/emscripten-core/emsdk ~/emsdk &&
#               ~/emsdk/emsdk install latest && ~/emsdk/emsdk activate latest
# or set $EMSDK to an existing emsdk root.  Requires cmake + make.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="$HERE/../src/wasm"

if ! command -v emcc >/dev/null 2>&1; then
    EMSDK="${EMSDK:-$HOME/emsdk}"
    # shellcheck disable=SC1091
    source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1 || true
fi
command -v emcc >/dev/null 2>&1 || {
    echo "error: emcc not found — install emsdk first (see AGENTS.md '构建 WASM')" >&2
    exit 1
}

TOOLCHAIN="$(em-config EMSCRIPTEN_ROOT)/cmake/Modules/Platform/Emscripten.cmake"
BUILD="$HERE/build-wasm"

cmake -S "$HERE" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN"
cmake --build "$BUILD" --target lcr_wasm -j"$(nproc)"

mkdir -p "$OUT_DIR"
cp "$BUILD/lcr_wasm.js" "$BUILD/lcr_wasm.wasm" "$OUT_DIR/"
echo "OK -> $OUT_DIR/lcr_wasm.{js,wasm}"
