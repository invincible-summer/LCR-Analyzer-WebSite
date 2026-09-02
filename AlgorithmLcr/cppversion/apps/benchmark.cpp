// Benchmark: measures C++ identification throughput for the report.
//   ./benchmark            -- 12-DUT suite, noisy + noiseless
//   ./benchmark --sweep N  -- additionally run N cases of the n<=5 topology
//                             sweep pattern (default 100)

#include "identify.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace rlc;

static double runSuite(double sigma) {
    auto duts = makeDuts();
    double total = 0.0;
    for (const auto& dut : duts) {
        Measurement ms = measure(dut, nullptr, sigma, 0);
        Config cfg;
        if (nLeaves(dut.tree) > 4) cfg.maxN = 5;
        auto t0 = std::chrono::steady_clock::now();
        identify(ms.f, ms.z, nullptr, &cfg);
        total += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    return total;
}

int main(int argc, char** argv) {
    int sweepN = 0;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--sweep") == 0 && i + 1 < argc)
            sweepN = std::atoi(argv[++i]);

    std::printf("C++ benchmark (single-threaded identify calls)\n");
    double t1 = runSuite(0.005);
    std::printf("  12-DUT noisy suite   : %7.3f s total (%.1f ms/DUT)\n", t1, t1 * 1e3 / 12);
    double t2 = runSuite(0.0);
    std::printf("  12-DUT noiseless     : %7.3f s total (%.1f ms/DUT)\n", t2, t2 * 1e3 / 12);

    if (sweepN > 0) {
        warmLibraryCache(5, 2);
        const std::vector<TreePtr>& lib = TopologyLibrary::get(5, 2);
        auto freqs = defaultFrequencies();
        double total = 0.0;
        int done = 0;
        for (int i = 0; i < sweepN && i < (int)lib.size() * 8; ++i) {
            const TreePtr& truth = lib[i % lib.size()];
            Rng rng(100000 + i);
            auto kinds = leafKinds(truth);
            std::vector<double> lb, ub;
            thetaBounds(truth, lb, ub);
            std::vector<double> theta(kinds.size());
            for (size_t j = 0; j < kinds.size(); ++j)
                theta[j] = lb[j] + 0.5 + rng.uniform01() * (ub[j] - lb[j] - 1.0);
            const size_t m = freqs.size();
            std::vector<Complex> z(m);
            evalThetaFreq(truth, theta, freqs.data(), m, z.data());
            Config cfg;
            cfg.maxN = std::max(4, nLeaves(truth));
            auto t0 = std::chrono::steady_clock::now();
            identify(freqs, z, nullptr, &cfg);
            total += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
                         .count();
            ++done;
        }
        std::printf("  sweep pattern x%-5d : %7.3f s total (%.1f ms/case)\n", done, total,
                    total * 1e3 / std::max(done, 1));
    }
    return 0;
}
