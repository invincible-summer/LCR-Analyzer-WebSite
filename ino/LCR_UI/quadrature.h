#pragma once

#include <stdint.h>

// Full 2-bit Gray-code quadrature transition decoder.
// State encoding is (A << 1) | B. A valid adjacent transition contributes
// exactly +/-1 quarter-step; same-state bounce contributes 0; a two-bit jump
// is invalid and is reported as 0 so the caller can reset its partial detent.
// The positive sequence 00 -> 01 -> 11 -> 10 -> 00 matches the firmware's
// historical EncInc direction.
inline constexpr int8_t quadratureTransition(uint8_t previous, uint8_t current)
{
    constexpr int8_t lut[16] = {
         0, +1, -1,  0,
        -1,  0,  0, +1,
        +1,  0,  0, -1,
         0, -1, +1,  0,
    };
    return lut[((previous & 0x3u) << 2) | (current & 0x3u)];
}

inline constexpr bool quadratureInvalidJump(uint8_t previous, uint8_t current)
{
    return previous != current && quadratureTransition(previous, current) == 0;
}
