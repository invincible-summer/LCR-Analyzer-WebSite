#!/usr/bin/env bash
# ============================================================================
# build_check.sh —— 用 arduino-cli 编译 LCR_UI 固件（CI 式验证，不烧录）
# ----------------------------------------------------------------------------
# 目标硬件：自制 ESP32-S3 开发板（ESP32-S3-WROOM-1-N16R8）。
#   * FQBN：esp32:esp32:esp32s3，PSRAM=opi（N16R8 八线 PSRAM）、
#     FlashSize=16M、CDCOnBoot=default（Disabled——本板 Micro USB 经 CH340X
#     隔离接 GPIO43/44，USB CDC 会把 Serial 引到 GPIO19/20 导致无输出；
#     见 docs/HARDWARE_MAPPING.md）。
#   * TFT_eSPI 引脚经编译期 -D 注入（ST7735S，与 board_profile.cpp 的
#     kBoard 一致）。v4.1.0：TFT 已移出 GPIO10-14（LCD_CAM 并行 DAC 总线
#     冲突），新映射 SCK=4 MOSI=5 CS=6 DC=7 RST=21（plan.md §10.4，待实物
#     continuity 确认）。改引脚只改 board_profile.cpp + 本文件 TFT_FLAGS。
#   * 任何 multiple definition / driver conflict 都是架构错误，不用链接器
#     workaround 掩盖（旧物理驱动文件必须删除而不是仅“不调用”）。
#
#   bash ino/tools/build_check.sh
# ============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKETCH="$HERE/../LCR_UI"

# arduino-cli 不在 PATH 时使用 ~/apps 下的安装
export PATH="$HOME/apps:$PATH"
command -v arduino-cli >/dev/null || { echo "arduino-cli not found"; exit 1; }

# TFT_eSPI 配置（与 board_profile.cpp 的 kBoard 保持一致）：
#   SCK=GPIO4 MOSI=GPIO5 CS=GPIO6 DC=GPIO7 RST=GPIO21；MISO 未用
#   ST7735S 写时钟最小 66ns → 10 MHz（升频前必须实屏压力测试）
TFT_FLAGS="-DUSER_SETUP_LOADED \
-DST7735_DRIVER \
-DTFT_WIDTH=128 -DTFT_HEIGHT=160 \
-DTFT_CS=6 -DTFT_DC=7 -DTFT_RST=21 \
-DTFT_MOSI=5 -DTFT_MISO=-1 -DTFT_SCLK=4 \
-DTFT_BL=-1 \
-DTOUCH_CS=-1 \
-DSPI_FREQUENCY=10000000 \
-DLOAD_GLCD -DLOAD_FONT2 -DLOAD_FONT4"

FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,CDCOnBoot=default,PartitionScheme=app3M_fat9M_16MB"

echo "== compile LCR_UI ($FQBN) =="
arduino-cli compile \
    --fqbn "$FQBN" \
    --build-property "compiler.cpp.extra_flags=${TFT_FLAGS}" \
    --warnings default \
    "$SKETCH"

echo "== BUILD OK =="
