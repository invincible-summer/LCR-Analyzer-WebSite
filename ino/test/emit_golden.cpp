// ============================================================================
// emit_golden.cpp —— 生成 golden one-port CSV fixture（stdout）
// ----------------------------------------------------------------------------
// 输出的 CSV 是固件 formatOnePortCsv 的真实产物（含一个失败点），
// 由前端 vitest 喂给 parseZCsv() 验证「固件 CSV ↔ 网站解析」位位兼容
// （plan.md §9.6 的软件侧验收）。golden 内容固定（无噪声/无时钟依赖），
// 由 CI 双侧锁定；格式语义变化必须 bump schema version。
// ============================================================================
#include "dataset.h"
#include "measurement_types.h"

#include <stdio.h>

int main()
{
    // 8 个频点（R=1kΩ 理想点 + actualHz 有微小偏移）+ 1 个失败点
    static OnePortPoint pts[9];
    const double f[8] = {100, 223.607, 500, 866.025, 1000.01, 1581.14,
                         2236.07, 5000};
    for (int i = 0; i < 8; ++i) {
        pts[i].requestedHz = f[i];
        pts[i].actualHz = f[i];
        pts[i].reOhm = 1000.0 + 2.0 * i;
        pts[i].imOhm = -3.0 * i + 1.5;
        pts[i].quality.status = MeasurementStatus::Ok;
        pts[i].quality.clippedChA = pts[i].quality.clippedChB = false;
        pts[i].quality.frequencyLocked = true;
        pts[i].quality.samplesA = pts[i].quality.samplesB = 512;
    }
    // 失败点（316Hz，SignalTooSmall）：不进 CSV
    pts[8].requestedHz = pts[8].actualHz = 316.0;
    pts[8].reOhm = pts[8].imOhm = 0.0;
    pts[8].quality.status = MeasurementStatus::SignalTooSmall;

    static char csv[ONEPORT_CSV_MAX];
    const size_t n = formatOnePortCsv(pts, 9, "factory-none", 1.05,
                                      csv, sizeof(csv));
    if (n == 0) return 1;
    fwrite(csv, 1, n, stdout);
    return 0;
}
