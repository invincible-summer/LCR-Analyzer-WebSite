// Dump the C++ identification results as JSON -- the mirror of
// tools/dump_python.py.  Compare both outputs with tools/compare.py.

#include "identify.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace rlc;

static std::string classify(const DUT& dut, const IdentifyResult& res,
                            const std::vector<double>& f, double equivTol, double& perr,
                            const Candidate*& repOut) {
    perr = -1.0;
    repOut = nullptr;
    if (res.classes.empty()) return "MISS";
    const EquivalenceClass& best = res.classes[0];
    const Candidate& rep = best.representative;
    repOut = &rep;
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

static void printEscaped(const std::string& s) { std::printf("%s", s.c_str()); }

int main(int argc, char** argv) {
    const char* outPath = argc > 1 ? argv[1] : "cpp_results.json";
    auto duts = makeDuts();
    std::ofstream out(outPath);
    out << "[\n";
    bool firstRec = true;
    for (const auto& dut : duts) {
        for (double sigma : {0.0, 0.005}) {
            Measurement ms = measure(dut, nullptr, sigma, 0);
            Config cfg;
            if (nLeaves(dut.tree) > 4) cfg.maxN = 5;
            auto t0 = std::chrono::steady_clock::now();
            IdentifyResult res = identify(ms.f, ms.z, nullptr, &cfg);
            double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
                            .count();
            double perr;
            const Candidate* rep = nullptr;
            double equivTol = sigma == 0.0 ? 1e-6 : 2e-2;
            std::string status = classify(dut, res, ms.f, equivTol, perr, rep);
            if (!firstRec) out << ",\n";
            firstRec = false;
            out << " {\"dut\": \"" << dut.name << "\", \"noise\": " << sigma
                << ", \"status\": \"" << status << "\", \"top1\": \""
                << (rep ? rep->canonicalStr() : "") << "\", \"theta\": [";
            if (rep) {
                for (size_t i = 0; i < rep->theta.size(); ++i) {
                    char b[40];
                    std::snprintf(b, sizeof(b), "%.12g", rep->theta[i]);
                    if (i) out << ", ";
                    out << b;
                }
            }
            out << "], \"wrmse\": ";
            char b[40];
            std::snprintf(b, sizeof(b), "%.6g", rep ? rep->wrmse : -1.0);
            out << b << ", \"param_err\": ";
            std::snprintf(b, sizeof(b), "%.6g", perr >= 0 ? perr : -1.0);
            out << b << ", \"seconds\": ";
            std::snprintf(b, sizeof(b), "%.6f", dt);
            out << b << "}";
        }
    }
    out << "\n]\n";
    (void)printEscaped;
    std::printf("wrote %s\n", outPath);
    return 0;
}
