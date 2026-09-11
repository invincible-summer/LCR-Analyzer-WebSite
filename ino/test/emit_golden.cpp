// ============================================================================
// emit_golden.cpp —— 生成 golden CSV fixtures（v2）
// ----------------------------------------------------------------------------
// 输出固件 formatOnePortCsv / formatTwoPortCsv 的真实产物（各含失败点），
// 由前端 vitest 喂给 parseZCsv()/parseHCsv() 验证「固件 CSV <-> 网站解析」
// 位位兼容（plan.md §9.4/§17.2 的软件侧验收）。golden 内容固定（无噪声/
// 无时钟依赖），由 CI 双侧锁定；格式语义变化必须 bump schema version。
// 用法：emit_golden <oneport.csv> <twoport.csv>
// ============================================================================
#include "dataset.h"
#include "measurement_types.h"

#include <math.h>
#include <stdio.h>

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s <oneport.csv> <twoport.csv>\n", argv[0]); return 2; }

    // ---- one-port：8 个有效点（R~1k 理想点）+ 1 个失败点（316Hz）----------
    static char csv[ONEPORT_CSV_MAX];
    {
        static ZPointRec ok[8];
        const double f[8] = {100, 223.607, 500, 866.025, 1000.01, 1581.14,
                             2236.07, 5000};
        for (int i = 0; i < 8; ++i) {
            ok[i].f = f[i];
            ok[i].re = 1000.0 + 2.0 * i;
            ok[i].im = -3.0 * i + 1.5;
        }
        const size_t n = formatOnePortCsv(ok, 8, "cal:3/10,open:ok,short:--",
                                          csv, sizeof(csv));
        if (n == 0) return 1;
        FILE* fp = fopen(argv[1], "wb");
        if (!fp) return 1;
        fwrite(csv, 1, n, fp);
        fclose(fp);
    }

    // ---- two-port：一阶 RC 低通 H=1/(1+j f/fc)，fc=1591.5 Hz，1 点失败 ----
    {
        static HPointRec ok[7];
        const double f[8] = {100, 193.079, 372.759, 719.686, 1389.5, 2682.7,
                             5179.47, 10000};
        const double fc = 1591.5;
        int m = 0;
        for (int i = 0; i < 8; ++i) {
            if (i == 3) continue;                    // 失败点：不进 CSV
            const double x = f[i] / fc;
            const double mag = 1.0 / sqrt(1.0 + x * x);
            const double ph = -atan(x) * 180.0 / 3.14159265358979323846;
            ok[m].f = f[i];
            ok[m].reH = mag * cos(ph * 3.14159265358979323846 / 180.0);
            ok[m].imH = mag * sin(ph * 3.14159265358979323846 / 180.0);
            ++m;
        }
        static char csv2[TWOPORT_CSV_MAX];
        const size_t n = formatTwoPortCsv(ok, m, "raw_w_path", csv2, sizeof(csv2));
        if (n == 0) return 1;
        FILE* fp = fopen(argv[2], "wb");
        if (!fp) return 1;
        fwrite(csv2, 1, n, fp);
        fclose(fp);
    }
    return 0;
}
