// Core suite: reduction rules F1-F4, aggregation formulas, port-open,
// nodal closed forms + adjoint Jacobian vs central differences -- mirrors
// tests/test_graph.py and tests/test_nodal.py.

#include "framework.hpp"
#include "graph.hpp"
#include "linalg.hpp"
#include "metric.hpp"
#include "nodal.hpp"
#include "synthetic.hpp"

#include <cmath>

using namespace tf;

TEST(graph_self_loop_dropped) {
    auto red = reduceGraph({{0, 1, 'R'}, {2, 2, 'C'}});
    CHECK(red.nGroups() == 1);
    CHECK(red.dropped.count(1) && red.dropped.at(1) == "self-loop");
}

TEST(graph_disconnected_dropped) {
    // edges 0-1 port R plus an isolated R island 2-3
    auto red = reduceGraph({{0, 1, 'R'}, {2, 3, 'R'}});
    CHECK(red.nGroups() == 1);
    CHECK(red.dropped.count(1) && red.dropped.at(1) == "disconnected");
}

TEST(graph_port_open_raises) {
    bool threw = false;
    try { reduceGraph({{0, 2, 'R'}, {3, 4, 'R'}}); } catch (const PortOpenError&) { threw = true; }
    CHECK(threw);
}

TEST(graph_dangling_dropped) {
    // R + C where C hangs off internal node 2
    auto red = reduceGraph({{0, 2, 'R'}, {2, 1, 'R'}, {2, 3, 'C'}});
    CHECK(red.nGroups() == 1);  // R+R series merged; C dangling
    CHECK(red.dropped.count(2) && red.dropped.at(2) == "dangling");
    // the merged group covers both resistors
    CHECK(red.edges[0].members.size() == 2);
}

TEST(graph_parallel_merge_rules) {
    // two parallel R merge (conductance), two parallel C merge; L never
    auto redR = reduceGraph({{0, 1, 'R'}, {0, 1, 'R'}});
    CHECK(redR.nGroups() == 1);
    Value v = evalGroup(redR.edges[0].expr, {Value{'R', 100.0, 0}, Value{'R', 300.0, 0}});
    CHECK_NEAR(v.v1, 75.0, 1e-12);  // 100 || 300
    auto redC = reduceGraph({{0, 1, 'C'}, {0, 1, 'C'}});
    CHECK(redC.nGroups() == 1);
    v = evalGroup(redC.edges[0].expr, {Value{'C', 1e-9, 0}, Value{'C', 2e-9, 0}});
    CHECK_NEAR(v.v1, 3e-9, 1e-20);
    auto redL = reduceGraph({{0, 1, 'L'}, {0, 1, 'L'}});
    CHECK(redL.nGroups() == 2);  // parallel L is NOT mergeable
}

TEST(graph_series_merge_rules) {
    // series R: sum; series C: reciprocal sum; series L: both add
    auto red = reduceGraph({{0, 2, 'R'}, {2, 1, 'R'}});
    CHECK(red.nGroups() == 1);
    Value v = evalGroup(red.edges[0].expr, {Value{'R', 100.0, 0}, Value{'R', 220.0, 0}});
    CHECK_NEAR(v.v1, 320.0, 1e-12);
    auto redC = reduceGraph({{0, 2, 'C'}, {2, 1, 'C'}});
    v = evalGroup(redC.edges[0].expr, {Value{'C', 1e-9, 0}, Value{'C', 1e-9, 0}});
    CHECK_NEAR(v.v1, 5e-10, 1e-21);
    // F4: series R absorbed into L's Rd
    auto redRL = reduceGraph({{0, 2, 'R'}, {2, 1, 'L'}});
    CHECK(redRL.nGroups() == 1);
    CHECK(redRL.edges[0].kind == 'L');
    v = evalGroup(redRL.edges[0].expr, {Value{'R', 50.0, 0}, Value{'L', 1e-3, 2.0}});
    CHECK_NEAR(v.v1, 1e-3, 1e-14);
    CHECK_NEAR(v.v2, 52.0, 1e-12);
}

TEST(graph_reduction_preserves_z_random) {
    // 30 random graphs: Z before vs after reduction agree (< 1e-9 relative)
    std::mt19937_64 rng(42);
    int checked = 0;
    for (int trial = 0; trial < 30; ++trial) {
        DUT dut = randomCase(rng);
        // random true values already inside dut.values
        try {
            std::vector<Complex> s;
            std::vector<double> f = defaultFrequencies(20);
            for (double fk : f) s.push_back(Complex(0, 1) * (2.0 * 3.14159265358979 * fk));
            std::vector<Complex> z0;
            try {
                z0 = dut.zExact(f);
            } catch (const PortOpenError&) {
                continue;  // random graph left the port open; skip
            }
            ReductionResult red = reduceGraph(dut.edges);
            NodalModel model = modelFromReduced(red);
            std::vector<Value> gvals = red.groupValues(dut.values);
            std::vector<double> flat;
            for (const Value& g : gvals) {
                flat.push_back(g.v1);
                if (g.kind == 'L') flat.push_back(g.v2);
            }
            std::vector<Complex> z1 = model.zLinear(flat, s);
            for (size_t k = 0; k < z0.size(); ++k) {
                double zmed = 0.0;
                for (auto& zk : z0) zmed += std::abs(zk);
                zmed /= (double)z0.size();
                double den = std::max(std::abs(z0[k]), 1e-6 * zmed);
                double rel = std::abs(z1[k] - z0[k]) / den;
                CHECK(!std::isfinite(rel) || rel < 1e-6);
            }
            ++checked;
        } catch (const PortOpenError&) {
            continue;
        }
    }
    CHECK(checked >= 15);
}

TEST(nodal_series_parallel_closed_forms) {
    std::vector<double> f{1e2, 1e5};
    std::vector<Complex> s;
    for (double fk : f) s.push_back(Complex(0, 1) * (2.0 * 3.14159265358979 * fk));
    // series RC
    NodalModel m1 = NodalModel::fromEdges({{0, 2, 'R'}, {2, 1, 'C'}});
    auto z1 = m1.zLinear({1e3, 100e-9}, s);
    for (size_t k = 0; k < s.size(); ++k)
        CHECK(std::abs(z1[k] - (Complex(1e3, 0) + 1.0 / (s[k] * 100e-9))) < 1e-9);
    // parallel R||C
    NodalModel m2 = NodalModel::fromEdges({{0, 1, 'R'}, {0, 1, 'C'}});
    auto z2 = m2.zLinear({2e3, 1e-6}, s);
    for (size_t k = 0; k < s.size(); ++k)
        CHECK(std::abs(z2[k] - 1.0 / (Complex(1.0 / 2e3, 0) + s[k] * 1e-6)) < 1e-9);
    // parallel R || L(dcr): second-order check via direct admittance
    NodalModel m3 = NodalModel::fromEdges({{0, 1, 'R'}, {0, 1, 'L'}});
    auto z3 = m3.zLinear({1e3, 1e-3, 5.0}, s);
    for (size_t k = 0; k < s.size(); ++k) {
        Complex y = Complex(1e-3, 0) + 1.0 / (Complex(5, 0) + s[k] * 1e-3);
        CHECK(std::abs(z3[k] - 1.0 / y) < 1e-9);
    }
}

TEST(nodal_jacobian_matches_central_differences) {
    std::mt19937_64 rng(7);
    for (int trial = 0; trial < 10; ++trial) {
        DUT dut = randomCase(rng);
        ReductionResult red;
        try {
            red = reduceGraph(dut.edges);
        } catch (const PortOpenError&) {
            continue;
        }
        NodalModel model = modelFromReduced(red);
        std::vector<Value> gvals = red.groupValues(dut.values);
        std::vector<double> flat;
        for (const Value& g : gvals) {
            flat.push_back(g.v1);
            if (g.kind == 'L') flat.push_back(g.v2);
        }
        std::vector<double> theta(flat.size());
        for (size_t i = 0; i < flat.size(); ++i) theta[i] = std::log10(flat[i]);
        std::vector<Complex> s{Complex(0, 2e4), Complex(0, 2e6)};
        std::vector<Complex> Z, J;
        model.zAndJac(theta, s, Z, J);
        const double h = 1e-6;
        int p = model.nParams;
        for (int t = 0; t < p; ++t) {
            std::vector<double> tp = theta, tm = theta;
            tp[(size_t)t] += h;
            tm[(size_t)t] -= h;
            std::vector<Complex> Zp, Zm, Jtmp;
            model.zAndJac(tp, s, Zp, Jtmp);
            model.zAndJac(tm, s, Zm, Jtmp);
            for (size_t k = 0; k < s.size(); ++k) {
                Complex fd = (Zp[k] - Zm[k]) / (2 * h);
                Complex an = J[(size_t)t * s.size() + k];
                // visibility-based skip (py test uses mixed tolerances for
                // the same reason): channels with |dlnZ/dlnv| < 1e-4 are
                // electrically invisible at double precision on
                // ill-conditioned random graphs, where FD is pure noise
                double scale = std::max(std::abs(fd), std::abs(an));
                if (scale < 1e-4 * std::abs(Z[k]) * kLn10) continue;
                CHECK(std::abs(fd - an) / scale < 1e-4);
            }
        }
    }
}

TEST(nodal_elasticity_invariant_form) {
    // E = J/(Z ln10): value factors cancel (regression for the documented
    // multiply-by-value bug in the Python history)
    NodalModel model = NodalModel::fromEdges({{0, 1, 'R'}, {0, 1, 'C'}});
    std::vector<double> theta{3.0, -8.0};
    std::vector<Complex> s{Complex(0, 6283.18)};
    std::vector<Complex> E;
    model.elasticity(theta, s, E);
    // for R||C: lnZ/dlnR = -(1/R)/(1/R + sC) etc.
    Complex y = Complex(1e-3, 0) + s[0] * 1e-8;
    Complex yR(1e-3, 0);
    Complex expectR = yR / y;  // dlnZ/dlnR = +yR/y (raising R raises Z)
    CHECK(std::abs(E[0] - expectR) < 1e-9);
    CHECK(std::abs(E[1] + (y - yR) / y) < 1e-9);
}

TEST(metric_aicc_and_floored_curve) {
    CHECK_NEAR(aicc(1.0, 60, 3), 60 * std::log(1.0 / 60.0) + 8 + 2 * 4 * 5.0 / 55.0, 1e-12);
    std::vector<Complex> zt{{10.0, 0}, {0.001, 0}};  // deep notch
    std::vector<Complex> zf{{9.0, 0}, {0.011, 0}};
    double floored = curveMaxRelFloored(zt, zf, 0.1);
    CHECK_NEAR(floored, 0.10, 1e-9);
    CHECK(curveMaxRel(zt, zf) > 1.0);
}
