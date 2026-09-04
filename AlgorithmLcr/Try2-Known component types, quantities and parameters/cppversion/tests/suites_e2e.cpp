// End-to-end suite: the 10 named DUTs, noiseless exact recovery + 0.5%
// noise robustness + funnel keeping the truth -- mirrors
// tests/test_end_to_end.py.

#include "framework.hpp"
#include "filters.hpp"
#include "identify.hpp"
#include "selector.hpp"
#include "synthetic.hpp"

#include <cmath>

using namespace ng;

// R4 contract: candidates may carry refined values (rep.comps); behavioral
// equivalence must evaluate each side with its own values.
static bool repMatchesTruth(const Candidate& rep, const Network& truth,
                            const ComponentSet& compset,
                            const std::vector<double>& grid, double tol) {
    const std::vector<Component>& rc =
        rep.comps.empty() ? compset.components() : rep.comps;
    std::vector<Complex> za = networkZValues(rep.network, rc, grid);
    std::vector<Complex> zb = networkZ(truth, compset, grid);
    return relDiffBelow(za, zb, tol);
}

TEST(e2e_noiseless_exact_recovery) {
    auto duts = makeDuts();
    int hits = 0;
    for (const auto& dut : duts) {
        std::vector<double> f = defaultFrequencies();
        std::vector<std::complex<double>> z = dut.zExact(f);
        Config cfg;
        IdentifyResult res = identify(dut.compset, f, z, nullptr, &cfg);
        CHECK(res.classes.size() >= 1);
        if (!res.classes.empty()) {
            const Candidate& rep = res.classes[0].representative;
            // noiseless: the truth must be recovered with machine precision
            CHECK(rep.wrmse < 1e-10);
            std::vector<double> grid = makeValidationGrid(f);
            CHECK(repMatchesTruth(rep, dut.network, dut.compset, grid, 1e-9));
            ++hits;
        }
    }
    CHECK(hits == (int)duts.size());
}

TEST(e2e_noisy_top1_contains_truth) {
    auto duts = makeDuts();
    int hits = 0;
    for (const auto& dut : duts) {
        std::vector<double> f = defaultFrequencies();
        std::vector<std::complex<double>> z = measureZ(dut.network, dut.compset, f, 0.005, 7);
        Config cfg;
        IdentifyResult res = identify(dut.compset, f, z, nullptr, &cfg);
        CHECK(!res.classes.empty());
        if (!res.classes.empty()) {
            const Candidate& rep = res.classes[0].representative;
            CHECK(rep.wrmse < 0.02);  // near the 0.5% noise floor
            std::vector<double> grid = makeValidationGrid(f);
            bool hit = repMatchesTruth(rep, dut.network, dut.compset, grid,
                                       std::max(1e-3, 3.0 * rep.wrmse));
            CHECK(hit);
            hits += hit;
        }
    }
    CHECK(hits == (int)duts.size());
}

TEST(e2e_funnel_keeps_truth) {
    // the probe funnel must never drop the true wiring: run the funnel on
    // noisy data of each named DUT and verify the truth network survives
    auto duts = makeDuts();
    for (const auto& dut : duts) {
        std::vector<double> f = defaultFrequencies();
        std::vector<std::complex<double>> z = measureZ(dut.network, dut.compset, f, 0.005, 3);
        std::vector<std::complex<double>> s(f.size());
        for (size_t k = 0; k < f.size(); ++k)
            s[k] = std::complex<double>(0.0, 1.0) * (2.0 * 3.14159265358979323846 * f[k]);
        std::vector<double> w = defaultWeights(z);
        FunnelState st = runFunnel(dut.compset, s, z, w, 3, 1e6, 200, 4096, false);
        std::vector<Network> kept = st.finalKeep();
        bool found = false;
        auto truthSerial = dut.network.serialize(dut.compset.components());
        for (const auto& net : kept) {
            if (net.serialize(dut.compset.components()) == truthSerial) {
                found = true;
                break;
            }
        }
        CHECK(found);
    }
}

TEST(e2e_random_duts_recovered) {
    // E=4 random admissible networks (seeded, mt19937 stream), noiseless:
    // top-1 must equal the truth wiring exactly (same orbit serial)
    std::mt19937_64 rng(20260903);
    ComponentSet cs = ComponentSet::make({1e3, 47e3}, {10e-9}, {{330e-6, 3.0}});
    for (int trial = 0; trial < 3; ++trial) {
        Network net = randomNetwork(cs, rng);
        std::vector<double> f = defaultFrequencies();
        std::vector<std::complex<double>> z = networkZ(net, cs, f);
        Config cfg;
        IdentifyResult res = identify(cs, f, z, nullptr, &cfg);
        CHECK(!res.classes.empty());
        if (!res.classes.empty()) {
            const Candidate& rep = res.classes[0].representative;
            CHECK(rep.wrmse < 1e-10);
            CHECK(rep.network.serialize(cs.components()) ==
                  net.serialize(cs.components()));
        }
    }
}
