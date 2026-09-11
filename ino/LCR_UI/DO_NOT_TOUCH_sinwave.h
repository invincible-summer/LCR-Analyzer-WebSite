#ifndef SINWAVE_H
#define SINWAVE_H

#include <stdlib.h>
#include <math.h>
#include <driver/periph_ctrl.h>
#include <esp_private/gdma.h>
#include <esp_rom_gpio.h>
#include <hal/gpio_hal.h>
#include <soc/lcd_cam_struct.h>
#include <hal/dma_types.h>
#include "esp_cache.h"     // ★ 新增：PSRAM cache 冲洗
#include "esp_memory_utils.h"    // ★ 新增：esp_ptr_external_ram 判断
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ==========================================
// 几何参数（唯一来源，freq_calc.h 直接引用）
// ==========================================
#define MAX_SINGLE_LEN 4092                              // 单描述符硬件上限不变（模式A判据继续用它）
#define DMA_CHUNK_LEN  4064                              // ★ 新增：链式分块长度 = 32×127，保证段首 32B 对齐
#define DMA_DESC_COUNT 16
#define MAX_CHAIN_LEN (DMA_DESC_COUNT * DMA_CHUNK_LEN)   // ★ 523776 → 520192 (128×4064)

// ==========================================
// ★ 新增：抖动（Dither）模块
// ==========================================
#define SIN_DITHER_EN 1   // 1=开启抖动，0=关闭（留作示波器 A/B 对比验证）

// xorshift32：纯软件 PRNG，每次调用约 5 条指令，周期 2^32-1
// 固定非零种子 → 每次生成的表确定性相同 → 残余为固定偏置，可被标定扣除
static uint32_t dither_rng_state = 0x2545F491;

// --- 定义 GPIO ---
#define PIN_D0 18
#define PIN_D1 8
#define PIN_D2 9
#define PIN_D3 10
#define PIN_D4 11
#define PIN_D5 12
#define PIN_D6 13
#define PIN_D7 14

// --- 全局硬件变量 ---
// ★ 描述符必须留在内部 RAM（GDMA 取描述符只认内部 RAM），只有数据 buffer 进 PSRAM
dma_descriptor_t DRAM_ATTR descriptors[DMA_DESC_COUNT];
gdma_channel_handle_t DRAM_ATTR dma_chan;
portMUX_TYPE DRAM_ATTR dma_mux = portMUX_INITIALIZER_UNLOCKED;

// --- 函数声明 ---
void IRAM_ATTR attachPinToSignal(int pin, int signal);
void IRAM_ATTR write_sin(uint32_t length, uint8_t sinbuffer[]);              // ★ uint16_t → uint32_t
void IRAM_ATTR init_lcd_cam_dma();
void IRAM_ATTR out_sin(uint32_t length, uint8_t sinbuffer[], uint8_t div);   // ★ uint16_t → uint32_t
void IRAM_ATTR stop_sin();

// --- 内部统一处理函数声明 ---
static void _out_sin_process(int length, uint8_t sinbuffer[], uint8_t div);

// --- 函数实现 ---

void IRAM_ATTR attachPinToSignal(int pin, int signal) {
  esp_rom_gpio_connect_out_signal(pin, signal, false, false);
  gpio_ll_func_sel(GPIO_LL_GET_HW(0), (gpio_num_t)pin, PIN_FUNC_GPIO);
  gpio_set_drive_capability((gpio_num_t)pin, (gpio_drive_cap_t)3);
}

static inline IRAM_ATTR uint32_t xorshift32(void) {
    uint32_t x = dither_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    dither_rng_state = x;
    return x;
}

// TPDF 三角抖动：取两个独立随机位相加减 1 → {-1, 0, +1}，概率 {1/4, 1/2, 1/4}
// 峰峰 2 LSB 是完全线性化量化器的最小抖动量（Vanderkooy–Lipshitz 条件）
// 注意：不要用两个 2bit 随机数相加减 2 —— 那会得到 -2..+4 的不对称分布（上次示例代码的坑）
static inline IRAM_ATTR int8_t tpdf_dither(void) {
    uint32_t x = xorshift32();
    return (int8_t)((((x >> 31) & 1) + ((x >> 30) & 1)) - 1);
}

// 可选：想每次开机图案不同，启动时调 dither_seed(esp_random())
static inline void dither_seed(uint32_t seed) {
    dither_rng_state = seed ? seed : 1u;   // 状态不能为 0
}


void IRAM_ATTR write_sin(uint32_t length, uint8_t sinbuffer[]) {
    for (uint32_t i = 0; i < length; i++) {
#if SIN_DITHER_EN
        // ★ 关键：抖动加在取整之前 = round(s + dither)，量化锯齿被彻底打散
        float s = 127.5f + 80.0f * sinf(2.0f * (float)PI * i / length);
        int v = (int)(s + tpdf_dither()+tpdf_dither() + 0.5f);   // round(s + dither)
        if (v < 0)   v = 0;    // 当前幅度 70 不会触发，纯保险
        if (v > 255) v = 255;
        sinbuffer[i] = (uint8_t)v;
#else
        // 原始行为，保持逐字节一致，用于 A/B 对比
        sinbuffer[i] = (uint8_t)(127.5 + 80.0 * sin(2.0 * PI * i / length));
#endif
    }
}

void IRAM_ATTR init_lcd_cam_dma() {
  int pins[8] = { PIN_D0, PIN_D1, PIN_D2, PIN_D3, PIN_D4, PIN_D5, PIN_D6, PIN_D7 };
  for (int i = 0; i < 8; i++) {
    attachPinToSignal(pins[i], LCD_DATA_OUT0_IDX + i);
  }
  gdma_channel_alloc_config_t dma_chan_config = {
    .direction = GDMA_CHANNEL_DIRECTION_TX,
  };
  gdma_new_channel(&dma_chan_config, &dma_chan);
  gdma_connect(dma_chan, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_LCD, 0));
  gdma_transfer_ability_t ability = {
    .sram_trans_align = 4,
    .psram_trans_align = 32,   // ★ 新增：EDMA 访问 PSRAM 的对齐配置
                              // （若你的 Arduino 核心版本无此字段，删除本行即可）
  };
  gdma_set_transfer_ability(dma_chan, &ability);
  periph_module_enable(PERIPH_LCD_CAM_MODULE);
  periph_module_reset(PERIPH_LCD_CAM_MODULE);
}

// ==========================================
// 核心统一处理函数
// ==========================================
static void IRAM_ATTR _out_sin_process(int length, uint8_t sinbuffer[], uint8_t div) {
  // 1. 生成正弦波数据 (在临界区外，减少耗时)
  write_sin((uint32_t)length, sinbuffer);

  // 1.5 ★★★ PSRAM cache 一致性（内部 RAM 时代不存在的问题）★★★
  //      CPU 写的数据还躺在 writeback cache 里，不冲洗 GDMA 经 EDMA 读 PSRAM
  //      会拿到旧数据。必须卡在 write_sin 之后、gdma_start 之前。
  //      buffer 在内部 RAM 时天然一致，esp_ptr_external_ram 判断后自动跳过。
  if (esp_ptr_external_ram(sinbuffer)) {
    uint32_t flush_len = ((uint32_t)length + 31u) & ~31u;   // 按 32B cache line 向上取整
    if (flush_len > (uint32_t)MAX_CHAIN_LEN) flush_len = MAX_CHAIN_LEN;
    esp_cache_msync(sinbuffer, flush_len, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  }

  portENTER_CRITICAL(&dma_mux);

  // 2. 重置 DMA
  gdma_reset(dma_chan);
  esp_rom_delay_us(5);

  // 3. 统一 DMA 描述符配置
  dma_descriptor_t *first_desc = NULL;
  dma_descriptor_t *prev_desc = NULL;

  if (length <= MAX_SINGLE_LEN) {
    // --- 模式 A: 单个描述符 (高频/短Buffer) ---
    descriptors[0].dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
    descriptors[0].dw0.suc_eof = 0;
    descriptors[0].dw0.size = length;
    descriptors[0].dw0.length = length;
    descriptors[0].buffer = (uint32_t *)sinbuffer;
    descriptors[0].next = &descriptors[0];
    first_desc = &descriptors[0];

  } else {
    // --- 模式 B: 链式描述符 (低频/长Buffer) ---
    int remaining_len = length;
    int offset = 0;
    int desc_idx = 0;

    while (remaining_len > 0 && desc_idx < DMA_DESC_COUNT) {
      int chunk_len = remaining_len;
      if (chunk_len > DMA_CHUNK_LEN) chunk_len = DMA_CHUNK_LEN;  // ★ MAX_SINGLE_LEN → DMA_CHUNK_LEN
      chunk_len = chunk_len & 0xFFFC;                            // 尾段保持 4 字节对齐（pts 本身是 4 的倍数）
      if (chunk_len == 0) break;

      descriptors[desc_idx].dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
      descriptors[desc_idx].dw0.suc_eof = 0;
      descriptors[desc_idx].dw0.size = chunk_len;
      descriptors[desc_idx].dw0.length = chunk_len;
      descriptors[desc_idx].buffer = (uint32_t *)(sinbuffer + offset);
      descriptors[desc_idx].next = NULL;

      if (desc_idx == 0) {
        first_desc = &descriptors[0];
      } else {
        prev_desc->next = &descriptors[desc_idx];
      }

      prev_desc = &descriptors[desc_idx];
      offset += chunk_len;
      remaining_len -= chunk_len;
      desc_idx++;
    }

    if (prev_desc && first_desc) {
      prev_desc->next = first_desc;
    }
  }

  // 4. LCD_CAM 时钟与控制参数
  LCD_CAM.lcd_clock.clk_en = 0;
  esp_rom_delay_us(1);
  LCD_CAM.lcd_clock.lcd_clk_sel = 3;
  LCD_CAM.lcd_clock.lcd_clkm_div_a = 0;
  LCD_CAM.lcd_clock.lcd_clkm_div_b = 0;
  LCD_CAM.lcd_clock.lcd_clkm_div_num = div;

  LCD_CAM.lcd_clock.lcd_ck_out_edge = 0;
  LCD_CAM.lcd_clock.lcd_ck_idle_edge = 0;
  LCD_CAM.lcd_clock.lcd_clk_equ_sysclk = 1;

  LCD_CAM.lcd_ctrl.lcd_rgb_mode_en = 0;
  LCD_CAM.lcd_user.lcd_2byte_en = 0;
  LCD_CAM.lcd_user.lcd_dout = 1;
  LCD_CAM.lcd_user.lcd_always_out_en = 1;
  LCD_CAM.lcd_misc.lcd_next_frame_en = 1;

  // 5. 启动流程（flush 已在上面完成，gdma_start 拿到的一定是干净数据）
  LCD_CAM.lcd_misc.lcd_afifo_reset = 1;
  esp_rom_delay_us(1);
  LCD_CAM.lcd_misc.lcd_afifo_reset = 0;
  gdma_start(dma_chan, (intptr_t)first_desc);

  esp_rom_delay_us(1);
  LCD_CAM.lcd_clock.clk_en = 1;
  esp_rom_delay_us(1);
  LCD_CAM.lcd_user.lcd_update = 1;
  portEXIT_CRITICAL(&dma_mux);

  // 6. 确保启动成功
  LCD_CAM.lcd_user.lcd_start = 1;
  esp_rom_delay_us(1000);
  while (1) {
    if (LCD_CAM.lcd_user.lcd_start == 1) break;
    LCD_CAM.lcd_user.lcd_start = 1;
    esp_rom_delay_us(1000);
  }
}

// ==========================================
// 对外接口 (傻瓜式包装)
// ==========================================

void IRAM_ATTR out_sin(uint32_t length, uint8_t sinbuffer[], uint8_t div) {
  if (length == 0 || length > MAX_CHAIN_LEN || div <= 1) return;
  LCD_CAM.lcd_user.lcd_start = 0;
  _out_sin_process((int)length, sinbuffer, div);
}

void IRAM_ATTR stop_sin() {
  LCD_CAM.lcd_user.lcd_start = 0;
}

#endif
