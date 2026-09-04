// Test runner: executes every suite, prints a per-suite summary and writes
// the full result table to test_results.txt.

#include "framework.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace rlctest;

struct SuiteEntry {
    const char* name;
    void (*fn)(TestCtx&);
};

int main(int argc, char** argv) {
    std::string filter;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--suite") == 0 && i + 1 < argc) filter = argv[++i];
    }

    std::vector<SuiteEntry> suites = {
        {"circuits", suiteCircuits},   {"library", suiteLibrary},
        {"engine_a", suiteEngineA},    {"engine_b", suiteEngineB},
        {"pruning", suitePruning},     {"selector", suiteSelector},
        {"end_to_end", suiteEndToEnd}, {"sweep_n1_5", suiteSweepN5},
        {"sweep_n6", suiteSweepN6},    {"noisy_sweep", suiteNoisySweep},
        {"extremes_bands", suiteExtremesBands},
        {"adjacency", suiteAdjacency}, {"iofmt", suiteIofmt},
        {"exact_n", suiteExactN},
    };

    long totalCases = 0, totalCaseFails = 0, totalChecks = 0, totalCheckFails = 0;
    std::vector<std::string> failureLines;

    std::printf("%-14s %10s %8s %12s %10s\n", "SUITE", "CASES", "FAILED", "CHECKS",
                "CHK-FAIL");
    std::printf("--------------------------------------------------------------\n");
    for (auto& s : suites) {
        if (!filter.empty() && filter != s.name) continue;
        TestCtx ctx;
        ctx.suite = s.name;
        s.fn(ctx);
        long caseFails = 0;
        std::map<std::string, int> categories;
        for (auto& c : ctx.cases) {
            if (!c.ok) {
                ++caseFails;
                char buf[512];
                std::snprintf(buf, sizeof(buf), "[%s] %s : %s", s.name, c.name.c_str(),
                              c.detail.c_str());
                failureLines.push_back(buf);
            }
            // sweep cases carry a [CATEGORY] prefix in their detail
            auto b = c.detail.find('[');
            auto e = c.detail.find(']');
            if (b != std::string::npos && e != std::string::npos && e > b)
                categories[c.detail.substr(b, e - b + 1)]++;
        }
        std::printf("%-14s %10zu %8ld %12ld %10ld\n", s.name, ctx.cases.size(), caseFails,
                    ctx.checks, ctx.checkFails);
        if (!categories.empty()) {
            std::string line = "  categories:";
            for (auto& kv : categories) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), " %s=%d", kv.first.c_str(), kv.second);
                line += buf;
            }
            std::printf("%s\n", line.c_str());
        }
        totalCases += (long)ctx.cases.size();
        totalCaseFails += caseFails;
        totalChecks += ctx.checks;
        totalCheckFails += ctx.checkFails;
    }
    std::printf("--------------------------------------------------------------\n");
    std::printf("TOTAL: %ld cases (%ld failed), %ld checks (%ld failed)\n", totalCases,
                totalCaseFails, totalChecks, totalCheckFails);

    if (!failureLines.empty()) {
        std::printf("\n%d failing case(s) (first 40 shown):\n", (int)failureLines.size());
        int shown = 0;
        for (auto& f : failureLines) {
            if (shown++ >= 40) break;
            std::printf("  %s\n", f.c_str());
        }
    }

    std::ofstream out("test_results.txt");
    out << "TOTAL: " << totalCases << " cases (" << totalCaseFails
        << " failed), checks: " << totalChecks << " (" << totalCheckFails << " failed)\n";
    for (auto& f : failureLines) out << f << "\n";
    return totalCaseFails == 0 ? 0 : 1;
}
