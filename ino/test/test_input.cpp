#include "check.h"
#include "quadrature.h"

#include <stdint.h>

static int runSequence(const uint8_t* s, int n)
{
    int q = 0, detents = 0;
    uint8_t prev = s[0];
    for (int i = 1; i < n; ++i) {
        const uint8_t cur = s[i];
        if (quadratureInvalidJump(prev, cur)) q = 0;
        else {
            q += quadratureTransition(prev, cur);
            if (q >= 4) { ++detents; q = 0; }
            else if (q <= -4) { --detents; q = 0; }
        }
        prev = cur;
    }
    return detents;
}

int main()
{
    {
        const uint8_t cw[] = {0, 1, 3, 2, 0};
        CHECK(runSequence(cw, 5) == 1);
    }
    {
        const uint8_t ccw[] = {0, 2, 3, 1, 0};
        CHECK(runSequence(ccw, 5) == -1);
    }
    {
        // Bouncy contact: adjacent backtracking must cancel rather than double count.
        const uint8_t bounce[] = {0, 1, 0, 1, 3, 1, 3, 2, 0};
        CHECK(runSequence(bounce, 9) == 1);
    }
    {
        // Impossible two-bit jump resets the partial detent, so it cannot be stitched
        // to later valid edges into a phantom click.
        const uint8_t glitch[] = {0, 1, 3, 0, 1, 3, 2, 0};
        CHECK(runSequence(glitch, 8) == 1);
        CHECK(quadratureInvalidJump(3, 0));
    }
    return 0;
}
