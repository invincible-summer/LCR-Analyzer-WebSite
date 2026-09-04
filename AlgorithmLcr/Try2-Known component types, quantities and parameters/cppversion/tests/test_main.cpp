#include "framework.hpp"

#include <cstdio>

int main() {
    int failedCases = 0;
    for (const auto& tc : testframework::registry()) {
        testframework::currentFailed() = false;
        std::printf("[ RUN] %s\n", tc.name.c_str());
        tc.fn();
        if (testframework::currentFailed()) {
            ++failedCases;
            std::printf("[FAIL] %s\n", tc.name.c_str());
        } else {
            std::printf("[ ok ] %s\n", tc.name.c_str());
        }
    }
    std::printf("\n%d/%zu cases passed, %d checks, %d failed checks\n",
                (int)testframework::registry().size() - failedCases,
                testframework::registry().size(), testframework::checks(),
                testframework::failures());
    return failedCases == 0 ? 0 : 1;
}
