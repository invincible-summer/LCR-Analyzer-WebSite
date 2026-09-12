#!/usr/bin/env bash
# ============================================================================
# build_check.sh —— 用 arduino-cli 编译 LCR_UI 固件（CI 式验证，不烧录）
# ----------------------------------------------------------------------------
# 目标硬件：自制 ESP32-S3 开发板（ESP32-S3-WROOM-1-N16R8）。
#   * 固定验证组合：Arduino-ESP32 3.3.11 + 已发布 TFT_eSPI 2.5.43。
#     TFT_eSPI 2.5.43 的 ESP32-S3 默认分支把 SPI_PORT 设为 Arduino FSPI；
#     Arduino-ESP32 3.x 在 S3 上定义 FSPI=0，而 TFT_eSPI 的 direct-register
#     路径使用的是外设号，因而默认路径会在 tft.init() 触发接近 0x10 的
#     非法寄存器写。2.5.43 同一 processor header 已提供 USE_FSPI_PORT：
#     该显式配置把 SPI_PORT 固定为 2（ESP32-S3 的用户 SPI2 外设）。因此
#     产品构建必须定义 USE_FSPI_PORT；display.cpp 再以 SPI_PORT==2 做
#     compile-time gate。上游后续代码也已把 S3 默认值直接修为 2。
#   * FQBN：esp32:esp32:esp32s3，PSRAM=opi（N16R8 八线 PSRAM）、
#     FlashSize=16M、CDCOnBoot=default（Disabled——本板 Micro USB 经 CH340X
#     隔离接 GPIO43/44，USB CDC 会把 Serial 引到 GPIO19/20 导致无输出；
#     见 docs/HARDWARE_MAPPING.md）。
#   * TFT_eSPI 引脚经编译期 -D 注入（ST7735S，与 board_profile.cpp 的
#     kBoard 一致）。TFT 已移出 GPIO10-14（LCD_CAM 并行 DAC 总线冲突），
#     映射 SCK=4 MOSI=5 CS=6 DC=7 RST=21（待实物 continuity）。
#   * ST7735S v1.3 Table 7：4-line serial write TSCYCW >= 66ns，理论上限
#     约 15.15MHz；产品构建固定 10MHz。
#   * 任何 multiple definition / driver conflict 都是架构错误，不用链接器
#     workaround 掩盖（旧物理驱动文件必须删除而不是仅“不调用”）。
#
#   bash ino/tools/build_check.sh
# ============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKETCH="$HERE/../LCR_UI"

export PATH="$HOME/apps:$PATH"
command -v arduino-cli >/dev/null || { echo "arduino-cli not found"; exit 1; }

# release/CI 复现入口：不接受“本机刚好有另一个版本”。
CORE_VER=$(arduino-cli core list 2>/dev/null | awk '$1=="esp32:esp32" {print $2; exit}')
TFT_VER=$(arduino-cli lib list 2>/dev/null | awk '$1=="TFT_eSPI" {print $2; exit}')
if [ "$CORE_VER" != "3.3.11" ]; then
    echo "ERROR: esp32:esp32 core 3.3.11 required, installed='${CORE_VER:-none}'" >&2
    echo "       arduino-cli core install esp32:esp32@3.3.11" >&2
    exit 2
fi
if [ "$TFT_VER" != "2.5.43" ]; then
    echo "ERROR: TFT_eSPI 2.5.43 required, installed='${TFT_VER:-none}'" >&2
    echo "       arduino-cli lib install TFT_eSPI@2.5.43" >&2
    exit 2
fi

# TFT_eSPI 配置（与 board_profile.cpp 的 kBoard 保持一致）：
#   USE_FSPI_PORT is mandatory on TFT_eSPI 2.5.43 + ESP32-S3: it makes the
#   library's S3 direct-register path select SPI_PORT=2 instead of default FSPI=0.
#   SCK=GPIO4 MOSI=GPIO5 CS=GPIO6 DC=GPIO7 RST=GPIO21；MISO 未用。
#   ST7735S 写时钟周期 >=66ns -> 10MHz 留出时序余量。
TFT_FLAGS="-DUSER_SETUP_LOADED \
-DUSE_FSPI_PORT \
-DST7735_DRIVER \
-DTFT_WIDTH=128 -DTFT_HEIGHT=160 \
-DTFT_CS=6 -DTFT_DC=7 -DTFT_RST=21 \
-DTFT_MOSI=5 -DTFT_MISO=-1 -DTFT_SCLK=4 \
-DTFT_BL=-1 \
-DTOUCH_CS=-1 \
-DSPI_FREQUENCY=10000000 \
-DLOAD_GLCD -DLOAD_FONT2 -DLOAD_FONT4"

FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,CDCOnBoot=default,PartitionScheme=app3M_fat9M_16MB"

echo "== toolchain: esp32:esp32 $CORE_VER / TFT_eSPI $TFT_VER =="
echo "== compile LCR_UI ($FQBN) =="
arduino-cli compile \
    --fqbn "$FQBN" \
    --build-property "compiler.cpp.extra_flags=${TFT_FLAGS}" \
    --warnings default \
    "$SKETCH"

echo "== BUILD OK =="
