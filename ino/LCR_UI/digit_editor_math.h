// ============================================================================
// digit_editor_math.h —— 十进制数位编辑的纯数学步进（host 可编译）
// ----------------------------------------------------------------------------
// 编码器编辑某一十进制位时，动作等价于对该位权值做 +/-1 次加法。
// 因此进位/借位由整数算术自然完成：例如 290 的十位 +1 -> 300，
// 300 的十位 -1 -> 290。最终结果再钳位到编辑器声明的 [vmin, vmax]。
// ============================================================================
#pragma once

#include <stdint.h>

inline int32_t digitEditorStep(int32_t value, int32_t vmin, int32_t vmax,
                               int ndigits, int pos, int direction)
{
    if (vmin > vmax) return value;
    if (value < vmin) value = vmin;
    if (value > vmax) value = vmax;
    if (ndigits <= 0 || ndigits > 9 || pos < 0 || pos >= ndigits ||
        (direction != 1 && direction != -1))
        return value;

    int64_t place = 1;
    for (int i = 0; i < ndigits - 1 - pos; ++i) place *= 10;

    int64_t next = (int64_t)value + (int64_t)direction * place;
    if (next < vmin) next = vmin;
    if (next > vmax) next = vmax;
    return (int32_t)next;
}
