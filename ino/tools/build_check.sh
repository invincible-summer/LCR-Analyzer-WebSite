#!/usr/bin/env bash
# ============================================================================
# build_check.sh —— 用 arduino-cli 编译 LCR_UI 固件（CI 式验证，不烧录）
# ----------------------------------------------------------------------------
# TFT_eSPI 库的引脚配置通过编译期 -D 定义注入（不修改库文件 User_Setup.h），
# 因此本脚本可在任何装好 TFT_eSPI 的机器上直接跑通：
#
#   bash ino/tools/build_check.sh
#
# 真正烧录到硬件时，请按 ino/README.md「TFT_eSPI 配置」一节修改库的
# User_Setup.h（内容与本文件的 TFT_* 定义保持一致即可）。
# ============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKETCH="$HERE/../LCR_UI"

# arduino-cli 不在 PATH 时使用 ~/apps 下的安装
export PATH="$HOME/apps:$PATH"
command -v arduino-cli >/dev/null || { echo "arduino-cli not found"; exit 1; }

# TFT_eSPI 配置（与 hw_config.h 的 TFT_PIN_* 一致）
TFT_FLAGS="-DUSER_SETUP_LOADED \
-DILI9341_DRIVER \
-DTFT_MOSI=23 -DTFT_MISO=19 -DTFT_SCLK=18 \
-DTFT_CS=5 -DTFT_DC=2 -DTFT_RST=4 \
-DLOAD_GLCD -DLOAD_FONT2 -DLOAD_FONT4 -DLOAD_FONT7 \
-DSPI_FREQUENCY=27000000"

echo "== compile LCR_UI (esp32:esp32:esp32) =="
arduino-cli compile \
    --fqbn esp32:esp32:esp32 \
    --build-property "compiler.cpp.extra_flags=${TFT_FLAGS}" \
    --warnings default \
    "$SKETCH"

echo "== BUILD OK =="
