#include "synthetic.hpp"

#include <cmath>

namespace rlc {

namespace {
Assembled ser(std::vector<Assembled> children) { return assemble(NK::Ser, std::move(children)); }
Assembled par(std::vector<Assembled> children) { return assemble(NK::Par, std::move(children)); }
Assembled leaf(char k, double v) { return Assembled{Tree::makeLeaf(k), {v}}; }
}  // namespace

std::vector<DUT> makeDuts() {
    struct Spec {
        const char* name;
        const char* group;
        Assembled a;
    };
    // 12 DUTs in 8 classes (DUT4 uses Cp = 50 pF, DESIGN.md section 8.2)
    std::vector<Spec> specs = {
        {"dut1a_R", "1_single", leaf('R', 100.0)},
        {"dut1b_L", "1_single", leaf('L', 1e-3)},
        {"dut1c_C", "1_single", leaf('C', 1e-8)},
        {"dut2a_ser_RL", "2_series2", ser({leaf('R', 50.0), leaf('L', 1e-3)})},
        {"dut2b_ser_RC", "2_series2", ser({leaf('R', 1e3), leaf('C', 1e-10)})},
        {"dut3a_par_RC", "3_parallel2", par({leaf('R', 1e3), leaf('C', 1e-8)})},
        {"dut3b_par_RL", "3_parallel2", par({leaf('R', 50.0), leaf('L', 1e-4)})},
        // inductor parasitic model: (Rs + L) || Cp, self-resonance ~7.1 MHz
        {"dut4_ind_parasitic", "4_ind_parasitic",
         par({ser({leaf('R', 1.0), leaf('L', 1e-5)}), leaf('C', 50e-12)})},
        // capacitor parasitic model: Resr + Lesl + C, series resonance ~1.1 MHz
        {"dut5_cap_parasitic", "5_cap_parasitic",
         ser({leaf('R', 0.05), leaf('L', 2e-9), leaf('C', 1e-5)})},
        // series-parallel mix: R1 + (R2 || C), relaxation pole ~1.6 kHz
        {"dut6_relaxation", "6_mixed3",
         ser({leaf('R', 100.0), par({leaf('R', 1e3), leaf('C', 1e-7)})})},
        // second-order tank: R || L || C, resonance ~5 MHz
        {"dut7_tank", "7_tank", par({leaf('R', 1e3), leaf('L', 1e-5), leaf('C', 1e-10)})},
        // four-element double peak: R1 + (L1||C1) + (L2||C2)
        {"dut8_double_peak", "8_double_peak",
         ser({leaf('R', 10.0), par({leaf('L', 1e-5), leaf('C', 1e-10)}),
              par({leaf('L', 1e-4), leaf('C', 1e-9)})})},
    };
    std::vector<DUT> duts;
    duts.reserve(specs.size());
    for (auto& s : specs) {
        DUT d;
        d.name = s.name;
        d.group = s.group;
        d.tree = s.a.tree;
        d.values = s.a.values;
        duts.push_back(std::move(d));
    }
    return duts;
}

std::vector<double> defaultFrequencies() { return geomspace(kFMin, kFMax, kNPoints); }

Measurement measure(const DUT& dut, const std::vector<double>* fIn, double sigmaRel,
                    uint64_t seed) {
    Measurement out;
    out.f = fIn ? *fIn : defaultFrequencies();
    const size_t m = out.f.size();
    std::vector<Complex> z = dut.zExact(out.f);
    if (sigmaRel > 0) {
        Rng rng(seed);
        std::vector<double> re(m), im(m);
        for (size_t k = 0; k < m; ++k) re[k] = rng.normal();
        for (size_t k = 0; k < m; ++k) im[k] = rng.normal();
        for (size_t k = 0; k < m; ++k)
            z[k] += sigmaRel * std::abs(z[k]) * Complex(re[k], im[k]);
    }
    out.z = std::move(z);
    return out;
}

double maxParamError(const std::vector<double>& thetaFit, const DUT& dut) {
    double worst = 0.0;
    for (size_t i = 0; i < thetaFit.size() && i < dut.values.size(); ++i) {
        double fit = std::pow(10.0, thetaFit[i]);
        double rel = std::fabs((fit - dut.values[i]) / dut.values[i]);
        worst = std::max(worst, rel);
    }
    return worst;
}

}  // namespace rlc
