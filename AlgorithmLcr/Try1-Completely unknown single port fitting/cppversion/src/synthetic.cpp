#include "synthetic.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

namespace rlc {

namespace {
Assembled ser(std::vector<Assembled> children) { return assemble(NK::Ser, std::move(children)); }
Assembled par(std::vector<Assembled> children) { return assemble(NK::Par, std::move(children)); }
Assembled leaf(char k, double v) { return Assembled{Tree::makeLeaf(k), {v}}; }
// a real inductor device: (leaf, [L, Rd])
Assembled ind(double l, double rd) { return Assembled{Tree::makeLeaf('L'), {l, rd}}; }

// Parameter blocks of interchangeable siblings: children with IDENTICAL
// canonical strings (two L leaves under one PAR node, or two structurally
// identical subtrees) are electrically interchangeable, so a fit may assign
// their parameter blocks in either order.
struct BlockGroup {
    std::vector<size_t> starts;  // start index of each block (canonical order)
    std::vector<size_t> widths;  // block widths (parameters per block)
};

void collectGroups(const Tree* t, size_t& offset, std::vector<BlockGroup>& groups) {
    if (t->isLeaf) {
        offset += (size_t)nParamsOfLeaf(t->elem);
        return;
    }
    std::vector<std::pair<std::string, size_t>> marks;  // (canonical, start)
    for (const auto& c : t->kids) {
        marks.emplace_back(canonical(c), offset);
        collectGroups(c.get(), offset, groups);
    }
    size_t i = 0;
    while (i < marks.size()) {
        size_t j = i;
        while (j + 1 < marks.size() && marks[j + 1].first == marks[i].first) ++j;
        if (j > i) {
            BlockGroup g;
            for (size_t k = i; k <= j; ++k) {
                g.starts.push_back(marks[k].second);
                g.widths.push_back((size_t)nParams(t->kids[k]));
            }
            groups.push_back(std::move(g));
        }
        i = j + 1;
    }
}

}  // namespace

std::vector<DUT> makeDuts() {
    struct Spec {
        const char* name;
        const char* group;
        Assembled a;
    };
    // 14 DUTs in 10 classes, v2 model: every inductor is real (L + Rd).
    // Values mirror rlc_id/synthetic.py (DESIGN.md section 11.6).
    std::vector<Spec> specs = {
        {"dut1a_R", "1_single", leaf('R', 100.0)},
        {"dut1b_L", "1_single", ind(1e-3, 5.0)},  // real 1 mH, 5 ohm winding
        {"dut1c_C", "1_single", leaf('C', 1e-8)},
        {"dut2a_ser_RC", "2_series2", ser({leaf('R', 1e3), leaf('C', 1e-10)})},
        // series resonance ~1.6 MHz, Q ~ 20 (Rd resolvable above 0.5% noise)
        {"dut2b_ser_LC", "2_series2", ser({ind(1e-5, 5.0), leaf('C', 1e-9)})},
        {"dut3a_par_RC", "3_parallel2", par({leaf('R', 1e3), leaf('C', 1e-8)})},
        {"dut3b_par_RL", "3_parallel2", par({leaf('R', 50.0), ind(1e-4, 2.0)})},
        // inductor parasitic model: (L with winding Rd) || Cp, self-res ~7.1 MHz
        {"dut4_ind_parasitic", "4_ind_parasitic",
         par({ind(1e-5, 1.0), leaf('C', 50e-12)})},
        // capacitor parasitic model: ESL (with its Rd) + C, res ~1.1 MHz
        {"dut5_cap_parasitic", "5_cap_parasitic",
         ser({ind(2e-9, 0.05), leaf('C', 1e-5)})},
        // series-parallel mix: R1 + (R2 || C), relaxation pole ~1.6 kHz
        {"dut6_relaxation", "6_mixed3",
         ser({leaf('R', 100.0), par({leaf('R', 1e3), leaf('C', 1e-7)})})},
        // second-order tank: R || L || C, resonance ~5 MHz
        {"dut7_tank", "7_tank", par({leaf('R', 1e3), ind(1e-5, 0.5), leaf('C', 1e-10)})},
        // five-device double peak, moderate tank Q (Rd resolvable above noise)
        {"dut8_double_peak", "8_double_peak",
         ser({leaf('R', 10.0), par({ind(1e-5, 10.0), leaf('C', 1e-10)}),
              par({ind(1e-4, 20.0), leaf('C', 1e-9)})})},
        // NEW v2 class: two real inductors in parallel (zeros 500 / 5e4,
        // pole 5e3 -- a decade from each)
        {"dut9_par_LL", "9_par_LL", par({ind(1e-2, 5.0), ind(1e-3, 50.0)})},
        // NEW v2 class: R + (L1 || L2), multi-L PAR inside a SER chain
        // (zeros 1e4 / 3e7, pole 3e5)
        {"dut10_ser_R_par_LL", "10_ser_R_par_LL",
         ser({leaf('R', 100.0), par({ind(1e-3, 10.0), ind(1e-5, 300.0)})})},
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
    // Minimize over interchangeable-sibling permutations (two L leaves under
    // one PAR node -- or two identical subtrees -- may legitimately have
    // their parameter blocks swapped by the fitter).
    const size_t n = dut.values.size();
    std::vector<BlockGroup> groups;
    size_t offset = 0;
    collectGroups(dut.tree.get(), offset, groups);

    std::vector<double> fit(n);
    for (size_t i = 0; i < n; ++i) fit[i] = std::pow(10.0, thetaFit[i]);

    // enumerate permutations per group (groups are tiny); product across
    // groups via recursive lambda
    double best = std::numeric_limits<double>::infinity();
    std::vector<size_t> idx(n);
    for (size_t i = 0; i < n; ++i) idx[i] = i;
    std::function<void(size_t)> rec = [&](size_t gi) {
        if (gi == groups.size()) {
            double worst = 0.0;
            for (size_t i = 0; i < n; ++i) {
                double rel = std::fabs((fit[idx[i]] - dut.values[i]) / dut.values[i]);
                worst = std::max(worst, rel);
            }
            best = std::min(best, worst);
            return;
        }
        const auto& g = groups[gi];
        std::vector<size_t> perm(g.starts.size());
        for (size_t i = 0; i < perm.size(); ++i) perm[i] = i;
        do {
            for (size_t slot = 0; slot < perm.size(); ++slot) {
                size_t src = perm[slot];
                for (size_t k = 0; k < g.widths[src]; ++k)
                    idx[g.starts[slot] + k] = g.starts[src] + k;
            }
            rec(gi + 1);
        } while (std::next_permutation(perm.begin(), perm.end()));
    };
    rec(0);
    return best;
}

}  // namespace rlc
