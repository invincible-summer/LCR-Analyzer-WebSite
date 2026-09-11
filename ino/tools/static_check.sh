#!/usr/bin/env bash
# ============================================================================
# static_check.sh —— plan.md §9.1 构建/静态验收的 grep 门禁
# ----------------------------------------------------------------------------
#   1. production source 中 BluetoothSerial 引用数 = 0（Classic BT 已删除）
#   2. 顶层业务 GPIO 仅由 BoardProfile 提供：board_profile.cpp 之外不允许
#      pinMode/digitalWrite/digitalRead/attachInterrupt 带裸数字
#   3. 合成 stub（partner_stubs 等）不参与生产构建
#   4. 固件版本宏存在且三处 schema/protocol 版本一致
#   5. 测量/射频互斥的 debug 断言源码存在（radio_lock）
# 用法：bash ino/tools/static_check.sh
# ============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/../LCR_UI"
FAIL=0

echo "== gate 1: no BluetoothSerial in production sources =="
if grep -rn "BluetoothSerial" "$SRC" 2>/dev/null; then
    echo "** FAIL: BluetoothSerial reference found **"; FAIL=1
else
    echo "OK (0 references)"
fi

echo "== gate 2: raw GPIO numbers only in board_profile.cpp =="
# 匹配 pinMode(数字… / digitalWrite(数字… / digitalRead(数字… / attachInterrupt(digitalPinToInterrupt(数字…
PATTERN='(pinMode|digitalWrite|digitalRead|analogRead|ledcAttachPin|attachInterrupt)\s*\(\s*[0-9]'
VIOL=$(grep -rn -E "$PATTERN" "$SRC" --include='*.cpp' --include='*.h' --include='*.ino' 2>/dev/null \
       | grep -v 'board_profile\.cpp' || true)
if [ -n "$VIOL" ]; then
    echo "** FAIL: bare GPIO usage outside board_profile.cpp **"
    echo "$VIOL"
    FAIL=1
else
    echo "OK (GPIO only via kBoard)"
fi

echo "== gate 3: no synthetic stubs in production sketch =="
STUB=$(ls "$SRC"/partner_stubs.cpp "$SRC"/bt_link.cpp "$SRC"/hw_config.h 2>/dev/null || true)
if [ -n "$STUB" ]; then
    echo "** FAIL: legacy/stub files present: $STUB **"; FAIL=1
else
    echo "OK (stubs removed from sketch)"
fi

echo "== gate 4: firmware/protocol/schema version macros =="
grep -q 'define LCR_FW_VERSION' "$SRC/fw_version.h" \
    && grep -q 'define LCR_BLE_PROTOCOL_VERSION 1' "$SRC/fw_version.h" \
    && grep -q 'define LCR_Z_CSV_SCHEMA_VERSION 1' "$SRC/fw_version.h" \
    && grep -q 'define LCR_H_CSV_SCHEMA_VERSION 1' "$SRC/fw_version.h" \
    && echo "OK" || { echo "** FAIL: version macros missing **"; FAIL=1; }

echo "== gate 5: radio/measurement mutex assertion present =="
grep -q 'radioLockInvariantOk' "$SRC/measurement_engine.cpp" \
    && grep -q 'radioLockInvariantOk' "$SRC/radio_manager.cpp" \
    && grep -q 'radioLockNotifyRadioActive' "$SRC/radio_manager.cpp" \
    && echo "OK" || { echo "** FAIL: radio_lock wiring missing **"; FAIL=1; }

echo "== gate 6: BLE off at boot (setup 不初始化射频) =="
grep -q 'startBleForSealedDataset' "$SRC/radio_manager.h" \
    && ! grep -rn 'BLEDevice::init' "$SRC/LCR_UI.ino" \
    && echo "OK" || { echo "** FAIL: BLE init path violates boot policy **"; FAIL=1; }

exit $FAIL
