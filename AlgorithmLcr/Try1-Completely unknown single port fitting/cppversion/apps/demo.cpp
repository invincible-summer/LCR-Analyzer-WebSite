// End-to-end demo mirroring demo.py: identify all 14 synthetic DUTs
// (v2 model: real inductors L + Rd) or a measurement file
// (../../../INPUT_FORMAT.md section 1).

#include "adjacency.hpp"
#include "identify.hpp"
#include "iofmt.hpp"
#include "report.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace rlc;

static std::string classify(const DUT& dut, const IdentifyResult& res,
                            const std::vector<double>& f, double equivTol, double& perr) {
    perr = -1.0;
    if (res.classes.empty()) return "MISS";
    const EquivalenceClass& best = res.classes[0];
    const Candidate& rep = best.representative;
    if (canonical(rep.tree) == canonical(dut.tree)) {
        perr = maxParamError(rep.theta, dut);
        return "EXACT";
    }
    Candidate truth;
    truth.tree = dut.tree;
    truth.theta = dut.theta();
    auto grid = makeValidationGrid(f);
    for (const auto& mem : best.members) {
        if (areEquivalent(mem, truth, grid, equivTol)) return "EQUIV";
    }
    return "MISS";
}

static int runMeasurements(const std::string& path, std::optional<int> exactN) {
    Measurements ms = loadMeasurements(path);
    Config cfg;
    cfg.maxN = 5;
    cfg.exactN = exactN;
    auto t0 = std::chrono::steady_clock::now();
    IdentifyResult res = identify(ms.f, ms.z, nullptr, &cfg);
    double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("rlc_id (C++ port) input mode | %s | %zu points | %s | %.2fs\n\n",
                path.c_str(), ms.f.size(),
                exactN ? ("exact_n=" + std::to_string(*exactN)).c_str() : "free search", dt);
    if (res.classes.empty()) {
        std::printf("(no valid candidates)\n");
        return 1;
    }
    const Candidate& rep = res.best()->representative;
    std::printf("top-1: %s   wRMSE=%.2e  (%d devices)\n",
                toString(rep.tree, &rep.theta).c_str(), rep.wrmse, nLeaves(rep.tree));
    std::printf("%s\n\n", candidateToAdjacency(rep).formatBlock("1").c_str());
    std::printf("%s\n", formatReport(path, res.classes, nullptr, 5).c_str());
    return 0;
}

static void usage() {
    std::fprintf(stderr,
                 "usage: demo [--noiseless] [--dut NAME] [--measurements FILE]\n"
                 "            [--exact-n N | --count FILE]\n");
}

int main(int argc, char** argv) {
    bool noiseless = false;
    const char* dutFilter = nullptr;
    const char* measurementsPath = nullptr;
    std::optional<int> exactN;
    const char* countPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--noiseless") == 0) noiseless = true;
        else if (std::strcmp(argv[i], "--dut") == 0 && i + 1 < argc) dutFilter = argv[++i];
        else if (std::strcmp(argv[i], "--measurements") == 0 && i + 1 < argc)
            measurementsPath = argv[++i];
        else if (std::strcmp(argv[i], "--exact-n") == 0 && i + 1 < argc)
            exactN = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--count") == 0 && i + 1 < argc) countPath = argv[++i];
        else { usage(); return 2; }
    }
    if (exactN.has_value() && countPath) {
        std::fprintf(stderr, "--exact-n and --count are mutually exclusive\n");
        return 2;
    }
    if (countPath) exactN = loadCount(countPath);
    if (exactN.has_value() && *exactN < 1) {
        std::fprintf(stderr, "--exact-n must be a positive integer\n");
        return 2;
    }
    if (measurementsPath) return runMeasurements(measurementsPath, exactN);

    double sigma = noiseless ? 0.0 : kDefaultSigmaRel;
    double equivTol = noiseless ? 1e-6 : 2e-2;

    auto dutsAll = makeDuts();
    std::vector<DUT> duts;
    for (const auto& d : dutsAll) {
        if (!dutFilter || std::strstr(d.name.c_str(), dutFilter)) duts.push_back(d);
    }
    if (duts.empty()) {
        std::printf("no DUT matches '%s'\n", dutFilter ? dutFilter : "");
        return 2;
    }
    std::printf("rlc_id (C++ port) end-to-end demo | %zu DUTs | sigma_rel=%g | band %g..%g Hz"
                " | %d pts%s\n\n",
                duts.size(), sigma, kFMin, kFMax, kNPoints,
                exactN ? (" | exact_n=" + std::to_string(*exactN)).c_str() : "");

    int nExact = 0, nEquiv = 0, nMiss = 0;
    auto tAll = std::chrono::steady_clock::now();
    for (const auto& dut : duts) {
        Measurement ms = measure(dut, nullptr, sigma, 0);
        Config cfg;
        if (nLeaves(dut.tree) > 4) cfg.maxN = 5;
        cfg.exactN = exactN;
        auto t0 = std::chrono::steady_clock::now();
        IdentifyResult res = identify(ms.f, ms.z, nullptr, &cfg);
        double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        double perr;
        std::string status = classify(dut, res, ms.f, equivTol, perr);
        nExact += status == "EXACT";
        nEquiv += status == "EQUIV";
        nMiss += status == "MISS";
        std::printf("[%-5s] %-22s (%4.2fs)\n", status.c_str(), dut.name.c_str(), dt);
        std::printf("        truth: %s\n", dut.describe().c_str());
        if (!res.classes.empty()) {
            const Candidate& rep = res.classes[0].representative;
            char perrTxt[48] = "";
            if (status == "EXACT")
                std::snprintf(perrTxt, sizeof(perrTxt), "  [param_err=%.2e]", perr);
            std::printf("        top-1: %s   wRMSE=%.2e%s\n",
                        toString(rep.tree, &rep.theta).c_str(), rep.wrmse, perrTxt);
            std::string adj = candidateToAdjacency(rep).formatBlock("1");
            for (size_t p = 0, q = 0; p != std::string::npos; p = q) {
                q = adj.find('\n', p);
                std::string line = adj.substr(p, q == std::string::npos ? q : q - p);
                if (q != std::string::npos) ++q;
                std::printf("        %s\n", line.c_str());
                if (q == std::string::npos) break;
            }
        }
        std::printf("\n");
    }
    double dtAll =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - tAll).count();
    std::printf("========================================================================\n");
    std::printf("SUMMARY: exact=%d  equivalent=%d  miss=%d  of %zu   (total %.2fs)\n", nExact,
                nEquiv, nMiss, duts.size(), dtAll);
    return nMiss == 0 ? 0 : 1;
}
