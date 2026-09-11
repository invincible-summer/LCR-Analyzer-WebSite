// check.h —— host 单测极简断言（ino/test 专用，不进固件）
#pragma once

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++g_checks;                                                        \
        if (!(cond)) {                                                     \
            ++g_failures;                                                  \
            fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);\
        }                                                                  \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                              \
    do {                                                                   \
        ++g_checks;                                                        \
        const double va_ = (double)(a), vb_ = (double)(b);                 \
        if (!(fabs(va_ - vb_) <= (tol))) {                                 \
            ++g_failures;                                                  \
            fprintf(stderr, "FAIL %s:%d  %s=%.8g vs %s=%.8g tol=%.8g\n",  \
                    __FILE__, __LINE__, #a, va_, #b, vb_, (double)(tol));  \
        }                                                                  \
    } while (0)

static int testSummary(const char* name)
{
    printf("%-24s %4d checks, %d failures\n", name, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
