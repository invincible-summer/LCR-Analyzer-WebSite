// End-to-end demo mirroring demo.py: identify the 10 named DUTs (noiseless
// or 0.5% noise) or a measurement file pair (measurements.txt +
// components.txt per ../../../INPUT_FORMAT.md).  --stats prints the
// enumeration count / timing table.

#include "adjacency.hpp"
#include "identify.hpp"
#include "iofmt.hpp"
#include "report.hpp"
#include "selector.hpp"
#include "synthetic.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace ng;

static void runStats() {
    std::printf("%2s %10s %12s %8s %9s %8s\n", "E", "structures", "candidates",
                "enum_s", "assign_s", "total_s");
    for (int E = 1; E <= 6; ++E) {
        auto t0 = std::chrono::steady_clock::now();
        const auto& structures = enumerateStructures(E);
        double tEnum =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::vector<Component> comps;
        const char* kinds[3] = {"R", "C", "L"};
        for (int i = 0; i < E; ++i)
            comps.push_back(makeComponent(kinds[i % 3][0], std::pow(10.0, i - 4)));
        ComponentSet cs(comps);
        auto t1 = std::chrono::steady_clock::now();
        long n = 0;
        for (const auto& st : structures) n += countAssignments(st, cs);
        double tAssign =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        std::printf("%2d %10zu %12ld %8.2f %9.2f %8.2f\n", E, structures.size(), n,
                    tEnum, tAssign, tEnum + tAssign);
    }
}

static int runFile(const std::string& measurementsPath,
                   const std::string& componentsPath) {
    Measurements ms = loadMeasurements(measurementsPath);
    ComponentSet cs = loadComponents(componentsPath);
    auto t0 = std::chrono::steady_clock::now();
    Config cfg;
    IdentifyResult res = identify(cs, ms.f, ms.z, nullptr, &cfg);
    double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("netgraph_id (C++ port) input mode | %s + %s | %zu points | %.2fs\n\n",
                measurementsPath.c_str(), componentsPath.c_str(), ms.f.size(), dt);
    if (res.classes.empty()) {
        std::printf("(no valid candidates)\n");
        return 1;
    }
    std::printf("%s\n\n", formatReport(measurementsPath, res.classes, cs, "", 8).c_str());
    int limit = std::min((int)res.classes.size(), 8);
    for (int rank = 1; rank <= limit; ++rank)
        std::printf("%s\n",
                    candidateToAdjacency(res.classes[rank - 1].representative, cs)
                        .formatBlock(std::to_string(rank))
                        .c_str());
    return 0;
}

int main(int argc, char** argv) {
    bool noiseless = false, stats = false;
    const char* measurementsPath = nullptr;
    const char* componentsPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--noiseless") == 0) noiseless = true;
        else if (std::strcmp(argv[i], "--stats") == 0) stats = true;
        else if (std::strcmp(argv[i], "--measurements") == 0 && i + 1 < argc)
            measurementsPath = argv[++i];
        else if (std::strcmp(argv[i], "--components") == 0 && i + 1 < argc)
            componentsPath = argv[++i];
        else {
            std::fprintf(stderr,
                         "usage: demo [--noiseless] [--stats] "
                         "[--measurements FILE --components FILE]\n");
            return 2;
        }
    }
    if (stats) {
        runStats();
        return 0;
    }
    if (measurementsPath || componentsPath) {
        if (!measurementsPath || !componentsPath) {
            std::fprintf(stderr,
                         "--measurements and --components must be given together\n");
            return 2;
        }
        return runFile(measurementsPath, componentsPath);
    }

    const double sigma = noiseless ? 0.0 : 0.005;
    auto duts = makeDuts();
    int nHit = 0;
    auto tAll = std::chrono::steady_clock::now();
    for (const auto& dut : duts) {
        std::vector<double> f = defaultFrequencies();
        std::vector<Complex> z = noiseless
                                     ? dut.zExact(f)
                                     : measureZ(dut.network, dut.compset, f, sigma, 7);
        auto t0 = std::chrono::steady_clock::now();
        Config cfg;
        IdentifyResult res = identify(dut.compset, f, z, nullptr, &cfg);
        double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        std::vector<double> grid = makeValidationGrid(f);
        bool hit = false;
        if (res.best() != nullptr) {
            double tol = std::max(1e-3, 3.0 * (res.best()->wrmse()));
            hit = areEquivalent(res.best()->representative.network, dut.network,
                                dut.compset, grid, tol);
        }
        nHit += hit;
        std::printf("%s\n",
                    formatReport(dut.name + " (E=" + std::to_string(dut.compset.n()) +
                                     (noiseless ? ", noiseless)" : ", 0.5% noise)"),
                                 res.classes, dut.compset, dut.describe(), 8)
                        .c_str());
        int limit = std::min((int)res.classes.size(), 8);
        for (int rank = 1; rank <= limit; ++rank)
            std::printf("%s\n",
                        candidateToAdjacency(res.classes[rank - 1].representative,
                                             dut.compset)
                            .formatBlock(std::to_string(rank))
                            .c_str());
        std::printf("[%s] candidates=%ld kept=%d structures=%d elapsed=%.2fs "
                    "(funnel=%.2fs eval=%.2fs cluster=%.2fs)\n\n",
                    hit ? "HIT " : "MISS", res.nCandidates, res.nFunnelKept,
                    res.nStructures, res.elapsed, res.tFunnel, res.tEval, res.tCluster);
        (void)dt;
    }
    double tAllS = std::chrono::duration<double>(std::chrono::steady_clock::now() - tAll).count();
    std::printf("== named DUTs: %d/%zu recovered == (%.2fs total)\n", nHit, duts.size(),
                tAllS);
    return nHit == (int)duts.size() ? 0 : 1;
}
