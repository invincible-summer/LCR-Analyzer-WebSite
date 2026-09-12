#!/usr/bin/env bash
# ============================================================================
# static_check.sh —— v4.1.0 DO_NOT_TOUCH 边界 / 硬件假设 / GPIO / 版本门禁
# ----------------------------------------------------------------------------
# Gate A  DO_NOT_TOUCH 文件 SHA-256 manifest（任何 hash 变化立即失败）
# Gate B  禁止引用 DO_NOT_TOUCH_hong.h（预留文件，不得 include/链接）
# Gate C  DNT API 只有一个应用层入口（ino/LCR_UI/lcr_api.cpp）
# Gate D  禁止绕过 API 直接调用 DNT 低层符号（out_freq/lcr_adc_/...）
# Gate E  禁止恢复错误硬件假设（PCM5102/I2S 激励/自写 ADC 链）
# Gate F  board_profile UI 引脚不得占用 DNT/受限 GPIO；TFT 编译 flags 必须
#         与 profile 一致，且 ST7735S 4-wire SCL 不得超过 datasheet 上限
# Gate G  BLE/测量互斥：radio_lock 在编排层与射频层都有接线
# Gate H  版本/schema 一致（fw_version.h / CSV_SCHEMA_V2 / 前端 fixture）
# Gate I  固件依赖冻结：Arduino-ESP32 3.3.11 + 已发布 TFT_eSPI 2.5.43；
#         必须显式 USE_FSPI_PORT 且 S3 direct-register SPI_PORT==2
# Gate J  Worker 可靠性：跨 task flag 使用 atomic、completion 不丢包、DNT
#         可能不写出参的局部结构必须零初始化并显式失败映射
# 用法：  bash ino/tools/static_check.sh
#         DNT_UPDATE_MANIFEST=1 bash ino/tools/static_check.sh  # 硬件团队更新后重锁
# ============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/../LCR_UI"
ROOT="$HERE/../.."
MANIFEST="$HERE/dnt_manifest.txt"
FAIL=0
fail() { echo "** FAIL: $1 **"; FAIL=1; }

prod_files() {
    find "$SRC" -maxdepth 1 -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.ino' \) \
        ! -name 'DO_NOT_TOUCH_*' ! -name '*.example' | sort
}

echo "== Gate A: DO_NOT_TOUCH SHA-256 manifest =="
if [ "${DNT_UPDATE_MANIFEST:-0}" = "1" ]; then
    (cd "$SRC" && sha256sum DO_NOT_TOUCH_freq_calc.h DO_NOT_TOUCH_hong.h \
        DO_NOT_TOUCH_lcr_adc.h DO_NOT_TOUCH_lcr_api.h DO_NOT_TOUCH_lcr_calib_core.h \
        DO_NOT_TOUCH_lcr_calib.h DO_NOT_TOUCH_lcr_diag.h DO_NOT_TOUCH_lcr_measure.h \
        DO_NOT_TOUCH_lcr_tone.h DO_NOT_TOUCH_sinwave.h \
        DO_NOT_TOUCH_EXAMPLE.ino.example) > "$MANIFEST"
    echo "manifest regenerated（硬件团队明确更新流程）"
fi
MAN_BAD=$(cd "$SRC" && sha256sum -c "$MANIFEST" 2>&1 >/dev/null | grep -v ': OK' || true)
if [ -n "$MAN_BAD" ]; then
    echo "$MAN_BAD"
    fail "DO_NOT_TOUCH 文件被修改（Gate A manifest 不匹配）"
else
    echo "OK (11 files unchanged)"
fi

echo "== Gate B: no DO_NOT_TOUCH_hong.h references in production code =="
HONG=$(grep -rl "DO_NOT_TOUCH_hong" "$SRC" --include=*.cpp --include=*.h --include=*.ino || true)
if [ -n "$HONG" ]; then
    echo "$HONG"
    fail "DO_NOT_TOUCH_hong.h 被生产代码引用"
else
    echo "OK (0 references)"
fi

echo "== Gate C: DNT API has exactly one app-layer include point =="
CNT=$(prod_files | xargs grep -l '#include "DO_NOT_TOUCH_lcr_api.h"' 2>/dev/null | wc -l)
if [ "$CNT" -eq 1 ] && grep -q '#include "DO_NOT_TOUCH_lcr_api.h"' "$SRC/lcr_api.cpp"; then
    echo "OK (only lcr_api.cpp)"
else
    fail "DO_NOT_TOUCH_lcr_api.h 的非 DNT include 点必须唯一（lcr_api.cpp），实际 $CNT 处"
fi
OTHERDNT=$(prod_files | xargs grep -n '#include "DO_NOT_TOUCH_' 2>/dev/null \
           | grep -v 'DO_NOT_TOUCH_lcr_api.h' || true)
if [ -n "$OTHERDNT" ]; then
    echo "$OTHERDNT"
    fail "生产代码直接 include 了 lcr_api.cpp 之外的 DO_NOT_TOUCH 头"
fi

echo "== Gate D: no low-level DNT symbols outside DO_NOT_TOUCH files =="
PAT='lcr_adc_|lcr_measure_|out_freq|out_sin|stop_sin|adc_continuous_|lcd_cam|gdma_|HC595_PIN_'
VIOL=$(prod_files | xargs grep -n -E "$PAT" 2>/dev/null \
        | grep -v '^ *//' | grep -v ':\s*//' || true)
if [ -n "$VIOL" ]; then
    echo "$VIOL"
    fail "应用层出现 DNT 低层符号（必须经 lcr_api wrapper）"
else
    echo "OK (0 low-level references)"
fi

echo "== Gate E: no stale hardware assumptions (PCM5102/I2S/custom ADC) =="
PAT2='PCM5102|excitation_driver|adc_capture|I2S excitation'
VIOL2=$(prod_files | xargs grep -n -E "$PAT2" 2>/dev/null || true)
if [ -n "$VIOL2" ]; then
    echo "$VIOL2"
    fail "生产源码出现已废止的硬件假设"
else
    echo "OK"
fi

echo "== Gate F: board_profile pins + TFT compile flags + ST7735S timing =="
python3 - "$SRC/board_profile.cpp" "$HERE/build_check.sh" << 'PYG' || fail "board_profile/TFT flags 与硬件约束不一致"
import re, sys
profile_path, build_path = sys.argv[1], sys.argv[2]
src = open(profile_path, encoding='utf-8').read()
build = open(build_path, encoding='utf-8').read()
PIN_FIELDS = {'tftCs','tftDc','tftRst','spiSck','spiMosi','spiMiso',
              'keyUp','keyDown','keyBack','keyOk','encA','encB','encSw'}
FORBIDDEN = set([1,2,8,9,10,11,12,13,14,15,16,17,18,
                 0,3,45,46,19,20,26,27,28,29,30,31,32,
                 33,34,35,36,37,43,44])
vals = {}
for m in re.finditer(r'\.(\w+)\s*=\s*(-?\d+)\s*,', src):
    vals[m.group(1)] = int(m.group(2))
# BoardProfile deliberately uses a named sentinel rather than magic -1.
if re.search(r'\.spiMiso\s*=\s*PIN_UNUSED\s*,', src):
    vals['spiMiso'] = -1
bad = [(f, vals[f]) for f in PIN_FIELDS if f in vals and vals[f] >= 0 and vals[f] in FORBIDDEN]
if bad:
    raise SystemExit(f'conflicting pins: {bad}')
missing = sorted(f for f in PIN_FIELDS if f not in vals)
if missing:
    raise SystemExit(f'unparsed pin fields: {missing}')
macro_to_field = {
    'TFT_CS':'tftCs', 'TFT_DC':'tftDc', 'TFT_RST':'tftRst',
    'TFT_SCLK':'spiSck', 'TFT_MOSI':'spiMosi', 'TFT_MISO':'spiMiso',
}
for macro, field in macro_to_field.items():
    mm = re.search(r'-D' + re.escape(macro) + r'=(-?\d+)', build)
    if not mm:
        raise SystemExit(f'missing {macro} in build_check.sh')
    actual = int(mm.group(1))
    if actual != vals[field]:
        raise SystemExit(f'{macro}={actual} != board_profile {field}={vals[field]}')
fm = re.search(r'-DSPI_FREQUENCY=(\d+)', build)
if not fm or 'tftSpiHz' not in vals:
    raise SystemExit('missing SPI_FREQUENCY or tftSpiHz')
freq = int(fm.group(1))
if freq != vals['tftSpiHz']:
    raise SystemExit(f'SPI_FREQUENCY={freq} != board_profile tftSpiHz={vals["tftSpiHz"]}')
if freq > 15_151_515:
    raise SystemExit(f'ST7735S SCL {freq} exceeds 66ns write-cycle limit')
if vals['spiMiso'] != -1:
    raise SystemExit('ST7735S product path is write-only; spiMiso must remain PIN_UNUSED/-1')
print('OK (UI pins safe; TFT flags match profile; SPI <= 15.15 MHz)')
PYG

echo "== Gate G: BLE/measurement mutex wiring =="
grep -q 'radioLockNotifyMeasurementActive' "$SRC/sweep_engine.cpp" \
    && grep -q 'radioLockNotifyRadioActive' "$SRC/radio_manager.cpp" \
    && grep -q 'radioLockInvariantOk' "$SRC/radio_manager.cpp" \
    && grep -q 'radioLockInvariantOk' "$SRC/sweep_engine.cpp" \
    && echo "OK (lock wired in orchestration + radio; runtime check in host tests)" \
    || fail "radio_lock 接线缺失"

echo "== Gate H: firmware/protocol/schema version consistency =="
grep -q 'define LCR_FW_VERSION' "$SRC/fw_version.h" \
    && grep -q 'define LCR_BLE_PROTOCOL_VERSION 1' "$SRC/fw_version.h" \
    && grep -q 'define LCR_Z_CSV_SCHEMA_VERSION 2' "$SRC/fw_version.h" \
    && grep -q 'define LCR_H_CSV_SCHEMA_VERSION 2' "$SRC/fw_version.h" \
    && [ -f "$ROOT/protocol/CSV_SCHEMA_V2.md" ] \
    && grep -q 'schema=lcr-z-csv-v2' "$ROOT/frontend/src/lib/__tests__/fixtures/golden_oneport.csv" \
    && echo "OK (fw 4.1.0 / protocol 1 / z-schema v2 / h-schema v2 / docs+fixture)" \
    || fail "版本或 schema 契约不一致"

echo "== Gate I: ESP32-S3/TFT dependency compatibility =="
CI="$ROOT/.github/workflows/ci.yml"
BUILD="$HERE/build_check.sh"
if grep -q 'esp32:esp32@3.3.11' "$CI" \
   && grep -q 'TFT_eSPI@2.5.43' "$CI" \
   && grep -q -- '-DUSE_FSPI_PORT' "$BUILD" \
   && grep -q 'SPI_PORT != 2' "$SRC/display.cpp" \
   && grep -q '15151515UL' "$SRC/display.cpp"; then
    echo "OK (core 3.3.11 + TFT_eSPI 2.5.43 + USE_FSPI_PORT; SPI_PORT=2 gate)"
else
    fail "ESP32-S3/TFT 依赖或 SPI host/timing gate 漂移"
fi

echo "== Gate J: Worker synchronization + no-unwritten-output reads =="
API="$SRC/lcr_api.cpp"
if grep -q '#include <atomic>' "$API" \
   && grep -q 'std::atomic<bool> s_ready' "$API" \
   && ! grep -q 'volatile bool s_ready' "$API" \
   && grep -q 'xQueueSend(s_eventQ, &ev, portMAX_DELAY)' "$API" \
   && grep -Fq 'LcrZPoint t[3]{};' "$API" \
   && grep -Fq 'LcrWPoint t[3]{};' "$API" \
   && grep -Fq 'LcrZPoint p{};' "$API" \
   && grep -Fq 'LcrCalcResult c{};' "$API" \
   && grep -Fq 'LcrCalStatus st{};' "$API" \
   && grep -q 'outputsWritten' "$API"; then
    echo "OK (atomic flags; reliable completion; DNT failure outputs guarded)"
else
    fail "Worker 同步/completion/出参失败路径保护不完整"
fi

exit $FAIL
