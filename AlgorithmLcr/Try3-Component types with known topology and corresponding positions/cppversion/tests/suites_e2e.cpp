// End-to-end suite: named DUT recovery, rank-deficiency diagnostics, weak
// parameter flags, AICc cross-graph ordering -- mirrors test_fit.py,
// test_identify.py and test_end_to_end.py.

#include "framework.hpp"
#include "fit.hpp"
#include "metric.hpp"
#include "synthetic.hpp"

#include <cmath>

using namespace tf;

TEST(e2e_noiseless_recovery_all_duts) {
    auto duts = makeDuts();
    int ok = 0;
    for (const auto& dut : duts) {
        auto [f, z] = measure(dut, nullptr, 0.0, 0);
        FitResult r = fitGraph(f, z, dut.edges, nullptr);
        CHECK(r.wrmse < 1e-9);  // curve recovered to machine precision
        ok += r.wrmse < 1e-9;
        // identifiable DUTs: parameters recovered precisely
        if (dut.name != "bridge") {
            ReductionResult red = reduceGraph(dut.edges);
            std::vector<Value> truth = red.groupValues(dut.values);
            std::vector<Value> fit = r.groupValues();
            auto [errs, labels] = matchedGroupErrors(fit, truth, red.edges);
            for (double e : errs) CHECK(e < 1e-6);
        }
    }
    CHECK(ok == (int)duts.size());
}

TEST(e2e_bridge_rank_deficient_flagged) {
    auto duts = makeDuts();
    const DUT* dut = nullptr;
    for (const auto& d : duts)
        if (d.name == "bridge") dut = &d;
    auto [f, z] = measure(*dut, nullptr, 0.0, 0);
    FitResult r = fitGraph(f, z, dut->edges, nullptr);
    CHECK(r.wrmse < 1e-9);           // curve fits perfectly ...
    CHECK(r.jacRank < r.nParams);    // ... but parameters are rank-deficient
    CHECK(r.nParams == 5);
}

TEST(e2e_weak_param_flagged_for_out_of_band_element) {
    // R || huge C: within the band the C dominates; R is invisible
    std::vector<std::tuple<int, int, char>> edges{{0, 1, 'R'}, {0, 1, 'C'}};
    std::vector<double> f = defaultFrequencies();
    NodalModel model = NodalModel::fromEdges(edges);
    std::vector<Complex> s(f.size());
    for (size_t k = 0; k < f.size(); ++k)
        s[k] = Complex(0, 1) * (2.0 * 3.14159265358979 * f[k]);
    std::vector<Complex> z = model.zLinear({1e6, 1e-3}, s);  // Xc << R in band
    FitResult r = fitGraph(f, z, edges, nullptr);
    CHECK(r.wrmse < 1e-6);
    bool rWeak = false;
    for (const auto& g : r.groups)
        if (g.kind == 'R' && !g.weakParams.empty()) rWeak = true;
    CHECK(rWeak);
}

TEST(e2e_identify_many_ranks_true_first) {
    auto duts = makeDuts();
    const DUT* dut = nullptr;
    for (const auto& d : duts)
        if (d.name == "ind_parasitic") dut = &d;
    auto [f, z] = measure(*dut, nullptr, 0.005, 3);
    std::vector<std::vector<std::tuple<int, int, char>>> graphs{
        {{0, 2, 'R'}, {2, 1, 'C'}, {0, 1, 'L'}}, dut->edges,
        {{0, 2, 'R'}, {2, 1, 'L'}, {0, 2, 'C'}}};
    FitConfig cfg;
    cfg.seed = 4;
    auto results = identifyMany(f, z, graphs, &cfg);
    CHECK(results.size() == 3);
    // AICc ascending
    CHECK(results[0].aiccVal <= results[1].aiccVal);
    CHECK(results[1].aiccVal <= results[2].aiccVal);
}

TEST(e2e_noisy_fits_at_floor) {
    auto duts = makeDuts();
    int ok = 0;
    for (const auto& dut : duts) {
        auto [f, z] = measure(dut, nullptr, 0.005, 0);
        FitConfig cfg;
        cfg.seed = 1;
        FitResult r = fitGraph(f, z, dut.edges, &cfg);
        ok += r.wrmse <= 0.021;  // 3x the 0.5% noise floor
    }
    CHECK(ok == (int)duts.size());
}
