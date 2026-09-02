// End-to-end demo mirroring demo.py: identify all 12 synthetic DUTs.

#include "identify.hpp"
#include "report.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
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

int main(int argc, char** argv) {
    bool noiseless = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--noiseless") == 0) noiseless = true;

    double sigma = noiseless ? 0.0 : kDefaultSigmaRel;
    double equivTol = noiseless ? 1e-6 : 2e-2;

    auto duts = makeDuts();
    std::printf("rlc_id (C++ port) end-to-end demo | %zu DUTs | sigma_rel=%g | band %g..%g Hz"
                " | %d pts\n\n",
                duts.size(), sigma, kFMin, kFMax, kNPoints);

    int nExact = 0, nEquiv = 0, nMiss = 0;
    auto tAll = std::chrono::steady_clock::now();
    for (const auto& dut : duts) {
        Measurement ms = measure(dut, nullptr, sigma, 0);
        Config cfg;
        if (nLeaves(dut.tree) > 4) cfg.maxN = 5;
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
