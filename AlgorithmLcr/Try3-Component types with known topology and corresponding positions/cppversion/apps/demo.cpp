// End-to-end demo mirroring demo.py: fit the 12 named DUTs (0.5% noise or
// noiseless); --ranking demos multi-candidate AICc ordering; file mode runs
// a measurements.txt + topology.txt pair (../../../INPUT_FORMAT.md 1/2.3).

#include "adjacency.hpp"
#include "fit.hpp"
#include "iofmt.hpp"
#include "synthetic.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace tf;

static int runFile(const std::string& measurementsPath, const std::string& topologyPath) {
    Measurements ms = loadMeasurements(measurementsPath);
    auto edges = loadTopology(topologyPath);
    auto t0 = std::chrono::steady_clock::now();
    FitResult r = fitGraph(ms.f, ms.z, edges, nullptr);
    double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("topofit_id (C++ port) input mode | %s + %s | %zu points | %.2fs\n\n",
                measurementsPath.c_str(), topologyPath.c_str(), ms.f.size(), dt);
    std::printf("%s\n", r.describe().c_str());
    std::printf("%s\n",
                fitresultToAdjacency(r).formatBlock("1", adjacencyNotes(r)).c_str());
    return 0;
}

int main(int argc, char** argv) {
    bool noiseless = false, ranking = false;
    const char* measurementsPath = nullptr;
    const char* topologyPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--noiseless") == 0) noiseless = true;
        else if (std::strcmp(argv[i], "--ranking") == 0) ranking = true;
        else if (std::strcmp(argv[i], "--measurements") == 0 && i + 1 < argc)
            measurementsPath = argv[++i];
        else if (std::strcmp(argv[i], "--topology") == 0 && i + 1 < argc)
            topologyPath = argv[++i];
        else {
            std::fprintf(stderr,
                         "usage: demo [--noiseless] [--ranking] "
                         "[--measurements FILE --topology FILE]\n");
            return 2;
        }
    }
    if (measurementsPath || topologyPath) {
        if (!measurementsPath || !topologyPath) {
            std::fprintf(stderr, "--measurements and --topology must be given together\n");
            return 2;
        }
        return runFile(measurementsPath, topologyPath);
    }

    const double sigma = noiseless ? 0.0 : 0.005;
    auto duts = makeDuts();
    if (ranking) {
        const DUT* dut = nullptr;
        for (const auto& d : duts)
            if (d.name == "ind_parasitic") dut = &d;
        auto [f, z] = measure(*dut, nullptr, sigma, 3);
        std::vector<std::vector<std::tuple<int, int, char>>> graphs{
            {{0, 2, 'R'}, {2, 1, 'C'}, {0, 1, 'L'}},
            dut->edges,
            {{0, 2, 'R'}, {2, 1, 'L'}, {0, 2, 'C'}}};
        std::printf("ranking demo on ind_parasitic (3 candidate graphs, sigma_rel=%g)\n",
                    sigma);
        auto results = identifyMany(f, z, graphs, nullptr);
        int rank = 1;
        for (const auto& r : results) {
            std::printf("\n#%d AICc=%.1f wRMSE=%.4g\n", rank, r.aiccVal, r.wrmse);
            std::printf("%s\n", r.describe().c_str());
            std::printf("%s\n",
                        fitresultToAdjacency(r)
                            .formatBlock(std::to_string(rank), adjacencyNotes(r))
                            .c_str());
            ++rank;
        }
        return 0;
    }

    int nOk = 0;
    double tAll = 0.0;
    for (const auto& dut : duts) {
        auto [f, z] = measure(dut, nullptr, sigma, 0);
        FitConfig cfg;
        cfg.seed = 1;
        auto t0 = std::chrono::steady_clock::now();
        FitResult r = fitGraph(f, z, dut.edges, &cfg);
        tAll += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        bool ok = r.wrmse <= (noiseless ? 1e-9 : 0.021);
        nOk += ok;
        std::printf("[%-4s] %-16s %s\n", ok ? "OK" : "BAD", dut.name.c_str(),
                    r.describe().c_str());
        std::printf("%s\n",
                    fitresultToAdjacency(r).formatBlock("", adjacencyNotes(r)).c_str());
        std::printf("\n");
    }
    std::printf("== named DUTs: %d/%zu fit at %s == (%.2fs total)\n", nOk, duts.size(),
                noiseless ? "machine precision" : "3x noise floor", tAll);
    return nOk == (int)duts.size() ? 0 : 1;
}
