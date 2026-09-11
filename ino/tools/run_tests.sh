#!/usr/bin/env bash
# ============================================================================
# run_tests.sh —— 在 PC（WSL/Ubuntu）上运行固件纯逻辑模块的本机单测
# ----------------------------------------------------------------------------
# 测试对象是与 Arduino 无关的模块（状态机/拟合/分类/CSV/协议帧），
# 直接用 g++ 编译运行，秒级反馈算法与状态机正确性。
# 用法：  bash ino/tools/run_tests.sh
# ============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/../LCR_UI"
TEST="$HERE/../test"
OUT="${TMPDIR:-/tmp}/lcr_host_tests"
mkdir -p "$OUT"

# 纯逻辑模块（host 可编译，无 Arduino 依赖）
PURE=(
    dsp_fit.cpp
    measurement_types.cpp
    measurement_engine.cpp
    sweep_engine.cpp
    dataset.cpp
    component_meter.cpp
    calibration.cpp
    radio_lock.cpp
    sine_plan.cpp
)
PURE_SRC=()
for f in "${PURE[@]}"; do PURE_SRC+=("$SRC/$f"); done

FAIL=0
for t in test_dsp test_engines test_component test_csv test_misc; do
    echo "== build+run $t =="
    g++ -std=c++17 -O2 -Wall -I"$SRC" -I"$TEST" \
        "$TEST/$t.cpp" "${PURE_SRC[@]}" -o "$OUT/$t"
    if ! "$OUT/$t"; then
        echo "** $t FAILED **"
        FAIL=1
    fi
done

# 生成 golden CSV fixture（供前端 vitest 校验 parseZCsv 兼容性）
echo "== emit golden one-port CSV fixture =="
g++ -std=c++17 -O2 -Wall -I"$SRC" -I"$TEST" \
    "$TEST/emit_golden.cpp" "${PURE_SRC[@]}" -o "$OUT/emit_golden"
"$OUT/emit_golden" > "$HERE/../../frontend/src/lib/__tests__/fixtures/golden_oneport.csv"
echo "wrote frontend/src/lib/__tests__/fixtures/golden_oneport.csv"

exit $FAIL
