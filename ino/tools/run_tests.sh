#!/usr/bin/env bash
# ============================================================================
# run_tests.sh —— 在 PC（WSL/Ubuntu）上运行 dsp_fit / analysis 的本机单测
# ----------------------------------------------------------------------------
# 这些模块是纯 C++（不依赖 Arduino），直接用 g++ 编译运行，秒级反馈算法正确性。
# 用法：  bash ino/tools/run_tests.sh
# ============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/../LCR_UI"
OUT="${TMPDIR:-/tmp}/lcr_test_dsp"

echo "== build test =="
g++ -std=c++17 -O2 -Wall -I"$SRC" \
    "$HERE/../test/test_dsp.cpp" "$SRC/dsp_fit.cpp" "$SRC/analysis.cpp" \
    -o "$OUT"

echo "== run test =="
"$OUT"
