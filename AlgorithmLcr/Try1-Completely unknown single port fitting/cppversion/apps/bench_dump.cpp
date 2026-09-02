// Export the exact benchmark sweep cases for Python replay: writes an
// index file ("<canonical> <maxN> <file>") plus one "<name>_<i>.txt" per
// case containing "Re Im" per frequency line (same format the failing-case
// dumper uses, so tools/xcheck.py style replay works).

#include "circuits.hpp"
#include "identify.hpp"
#include "library.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace rlc;

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "/tmp/bench_cases";
    int sweepN = argc > 2 ? std::atoi(argv[2]) : 120;
    warmLibraryCache(5, 2);
    const std::vector<TreePtr>& lib = TopologyLibrary::get(5, 2);
    auto freqs = defaultFrequencies();
    std::ofstream idx(std::string(dir) + "/index.txt");
    // second pass verdicts for engine-vs-engine comparison with the
    // Python replay (tools/bench_python.py writes the same file)
    std::ofstream vrd(std::string(dir) + "/cpp_verdict.txt");
    if (!idx || !vrd) {
        std::fprintf(stderr, "cannot write index in %s\n", dir);
        return 1;
    }
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
        std::string name = canonical(truth);
        std::string fname = name;
        for (char& c : fname)
            if (c == '(' || c == ')' || c == ',') c = '_';
        fname += "_" + std::to_string(i) + ".txt";
        std::ofstream out(std::string(dir) + "/" + fname);
        for (size_t k = 0; k < m; ++k)
            out << z[k].real() << " " << z[k].imag() << "\n";
        idx << name << " " << std::max(4, nLeaves(truth)) << " " << fname << "\n";
        Config cfg;
        cfg.maxN = std::max(4, nLeaves(truth));
        IdentifyResult res = identify(freqs, z, nullptr, &cfg);
        std::string top1;
        double wrmse = -1.0;
        if (!res.classes.empty()) {
            top1 = res.classes[0].representative.canonicalStr();
            wrmse = res.classes[0].representative.wrmse;
        }
        vrd << name << " | " << (top1.empty() ? "(none)" : top1) << " | "
            << wrmse << "\n";
    }
    std::printf("wrote %d cases to %s\n", sweepN, dir);
    return 0;
}
