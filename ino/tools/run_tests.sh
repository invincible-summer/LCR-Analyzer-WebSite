#!/usr/bin/env bash
# ============================================================================
# run_tests.sh —— 在 PC（WSL/Ubuntu）上运行固件编排层/纯逻辑模块的本机单测
# ----------------------------------------------------------------------------
# v4.1.x：host 测试不再模拟另一套 ADC —— 物理测量全部在 DO_NOT_TOUCH API
# 之后的真实硬件上。host 侧只测“编排 + 纯数学/格式化”，测量后端用
# MockLcrService（ino/test/test_mocks.h）注入。
# 覆盖频率网格/chunk/取消/seal/CSV/CRC/BLE 帧/metadata v2/H 换算/
# 单元件判型与聚合、StopTone quiet-guard 跨 millis() 回绕，以及 EC11
# quadrature 状态机的正反向/触点抖动/非法跳变回归。
# 用法：  bash ino/tools/run_tests.sh
# ============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/../LCR_UI"
TEST="$HERE/../test"
OUT="${TMPDIR:-/tmp}/lcr_host_tests"
mkdir -p "$OUT"

PURE=(
    measurement_types.cpp
    sweep_engine.cpp
    dataset.cpp
    component_meter.cpp
    radio_lock.cpp
)
PURE_SRC=()
for f in "${PURE[@]}"; do PURE_SRC+=("$SRC/$f"); done

FAIL=0
for t in test_sweep test_rollover test_component test_csv test_misc test_input; do
    echo "== build+run $t =="
    g++ -std=c++17 -O2 -Wall -Wextra -Werror=return-type \
        -I"$SRC" -I"$TEST" \
        "$TEST/$t.cpp" "${PURE_SRC[@]}" -o "$OUT/$t"
    if ! "$OUT/$t"; then
        echo "** $t FAILED **"
        FAIL=1
    fi
done

# Repeat-upload regression is partly a lifecycle invariant rather than a host-
# executable BLE test. Lock the source contract so the old irreversible path
# cannot silently return during later refactors.
echo "== static BLE lifecycle invariants =="
RADIO="$SRC/radio_manager.cpp"
if grep -q 'BLEDevice::deinit(true)' "$RADIO"; then
    echo "** BLE lifecycle FAILED: deinit(true) prevents reinitialization **"
    FAIL=1
fi
if ! grep -q 'BLEDevice::deinit(false)' "$RADIO" \
   || ! grep -q 'm_attPayload = kConservativeDataPayload' "$RADIO" \
   || ! grep -q 's_connectEvent' "$RADIO" \
   || ! grep -q 'kTxIntervalMs' "$RADIO"; then
    echo "** BLE lifecycle FAILED: missing repeat-session/MTU/mailbox/pacing guard **"
    FAIL=1
fi
if grep -R -n -E '^[[:space:]]*radio\.poll\(\);' "$SRC"/screen_*.cpp; then
    echo "** BLE lifecycle FAILED: screen must not double-pump radio.poll() **"
    FAIL=1
fi

FIXDIR="$HERE/../../frontend/src/lib/__tests__/fixtures"
echo "== emit golden CSV fixtures (v2) =="
g++ -std=c++17 -O2 -Wall -Wextra -Werror=return-type \
    -I"$SRC" -I"$TEST" \
    "$TEST/emit_golden.cpp" "${PURE_SRC[@]}" -o "$OUT/emit_golden"
"$OUT/emit_golden" "$FIXDIR/golden_oneport.csv" "$FIXDIR/golden_twoport.csv"
echo "wrote $FIXDIR/golden_oneport.csv + golden_twoport.csv"

exit $FAIL
