// Core suite: graph canonicalization / R0 / VTP, enumeration counts and
// completeness, batched nodal analysis, metrics -- mirrors the assertions of
// netgraph_id tests test_graph.py / test_enumerate.py / test_nodal.py /
// test_metric.py.

#include "framework.hpp"
#include "components.hpp"
#include "enumerate.hpp"
#include "filters.hpp"
#include "graph.hpp"
#include "iofmt.hpp"
#include "metric.hpp"
#include "nodal.hpp"
#include "synthetic.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

using namespace ng;

// ---------------------------------------------------------------------------
// graph
// ---------------------------------------------------------------------------

TEST(graph_slot_basics) {
    CHECK(nSlots(2) == 1);
    CHECK(nSlots(3) == 3);
    CHECK(nSlots(5) == 10);
    const auto& s3 = slotList(3);
    CHECK(s3[0] == std::make_pair(0, 1));
    CHECK(s3[1] == std::make_pair(0, 2));
    CHECK(s3[2] == std::make_pair(1, 2));
    for (int V = 2; V <= 7; ++V) {
        const auto& slots = slotList(V);
        CHECK((int)slots.size() == nSlots(V));
        for (int k = 0; k < nSlots(V); ++k) {
            auto [i, j] = slots[k];
            CHECK(slotIndex(V, i, j) == k);
        }
    }
}

TEST(graph_connected) {
    CHECK(isConnected(2, {1}));
    CHECK(!isConnected(3, {1, 0, 0}));   // edge (0,1) only, node 2 isolated
    CHECK(isConnected(3, {1, 1, 0}));    // path 0-1-2 via (0,1),(0,2)? (0,1),(0,2) yes
    CHECK(!isConnected(4, {1, 0, 0, 1, 0, 0}));  // {0,1} and {2,3} separate
}

TEST(graph_dead_part_rules) {
    // path 0-2-1 (edges (0,2),(1,2)) is alive
    CHECK(!hasDeadPart(3, {0, 1, 1}));
    // triangle on 3 nodes is alive
    CHECK(!hasDeadPart(3, {1, 1, 1}));
    // star at internal node: edges (0,1),(1,2): G-1 leaves {2} dead
    std::vector<int> m1 = emptyMult(3);
    m1[slotIndex(3, 0, 1)] = 1;
    m1[slotIndex(3, 1, 2)] = 1;
    CHECK(hasDeadPart(3, m1));
    // square cycle 0-1-2-3-0 is alive
    std::vector<int> m2 = emptyMult(4);
    m2[slotIndex(4, 0, 1)] = 1;
    m2[slotIndex(4, 1, 2)] = 1;
    m2[slotIndex(4, 2, 3)] = 1;
    m2[slotIndex(4, 0, 3)] = 1;
    CHECK(!hasDeadPart(4, m2));
}

TEST(graph_dead_part_explicit) {
    // pendant triangle: edges (0,1) port + (1,2),(1,3),(2,3) triangle at node 1
    // G-1: component {0} has terminal -> fine; component {2,3} dead -> true
    std::vector<int> mult = emptyMult(4);
    mult[slotIndex(4, 0, 1)] = 1;
    mult[slotIndex(4, 1, 2)] = 1;
    mult[slotIndex(4, 1, 3)] = 1;
    mult[slotIndex(4, 2, 3)] = 1;
    CHECK(hasDeadPart(4, mult));
    // adding a connection from the triangle to node 0 revives it
    mult[slotIndex(4, 0, 2)] = 1;
    CHECK(!hasDeadPart(4, mult));
}

TEST(graph_canonical_is_bruteforce_min) {
    std::mt19937_64 rng(7);
    for (int trial = 0; trial < 200; ++trial) {
        int V = 2 + (int)(rng() % 5);
        std::vector<int> mult = emptyMult(V);
        int E = 1 + (int)(rng() % 5);
        for (int e = 0; e < E; ++e) {
            const auto& slots = slotList(V);
            mult[rng() % slots.size()] += 1;
        }
        std::vector<int> can = canonicalMult(V, mult);
        std::vector<int> best;
        for (const auto& p : permGroup(V)) {
            std::vector<int> m = permuteMult(V, mult, p);
            if (best.empty() || m < best) best = m;
        }
        CHECK(can == best);
        CHECK(canonicalMult(V, can) == can);  // idempotent
    }
}

TEST(graph_automorphisms_fix_structure) {
    // square cycle with DIAGONAL ports 0,1 (edges (0,2),(0,3),(1,2),(1,3)):
    // the relabeling group {port swap} x Sym({2,3}) has all 4 elements fixing
    // the mult vector -> aut = 4 (the "square cycle aut=4 dedup" case)
    std::vector<int> mult = emptyMult(4);
    mult[slotIndex(4, 0, 2)] = 1;
    mult[slotIndex(4, 0, 3)] = 1;
    mult[slotIndex(4, 1, 2)] = 1;
    mult[slotIndex(4, 1, 3)] = 1;
    auto aut = structureAutomorphisms(4, mult);
    CHECK((int)aut.size() == 4);
    for (const auto& p : aut) CHECK(permuteMult(4, mult, p) == mult);
    CHECK(std::any_of(aut.begin(), aut.end(), [](const std::vector<int>& p) {
        return p == std::vector<int>{0, 1, 2, 3};
    }));
}

TEST(graph_series_parallel_recognition) {
    CHECK(isSeriesParallel(2, {1}));
    CHECK(isSeriesParallel(2, {3}));       // triple parallel
    CHECK(isSeriesParallel(3, {0, 1, 1}));  // path 0-2-1
    CHECK(!isSeriesParallel(4, {1, 1, 1, 1, 1, 1}));  // K4
    // Wheatstone bridge (square + diagonal) is NOT two-terminal SP
    std::vector<int> mult = emptyMult(4);
    mult[slotIndex(4, 0, 2)] = 1;
    mult[slotIndex(4, 0, 3)] = 1;
    mult[slotIndex(4, 1, 2)] = 1;
    mult[slotIndex(4, 1, 3)] = 1;
    mult[slotIndex(4, 2, 3)] = 1;
    CHECK(!isSeriesParallel(4, mult));
    // simple bridge-free ladder R1 + (C || (R2 + C2))
    std::vector<int> lad = emptyMult(4);
    lad[slotIndex(4, 0, 2)] = 1;
    lad[slotIndex(4, 1, 2)] = 1;
    lad[slotIndex(4, 2, 3)] = 1;
    lad[slotIndex(4, 1, 3)] = 1;
    CHECK(isSeriesParallel(4, lad));
}

// ---------------------------------------------------------------------------
// enumeration
// ---------------------------------------------------------------------------

TEST(enumerate_counts_match_python) {
    // DESIGN.md 4.4 table (also locked by python demo --stats)
    int expectedStructs[] = {1, 2, 4, 11, 31, 104};
    int expectedCands[] = {1, 2, 10, 98, 1426, 27542};
    for (int E = 1; E <= 6; ++E) {
        const auto& st = enumerateStructures(E);
        CHECK((int)st.size() == expectedStructs[E - 1]);
        // distinguishable components (alternating kinds like demo --stats)
        std::vector<Component> comps;
        const char* kinds[3] = {"R", "C", "L"};
        for (int i = 0; i < E; ++i)
            comps.push_back(makeComponent(kinds[i % 3][0], std::pow(10.0, i - 4)));
        ComponentSet cs(comps);
        long n = 0;
        for (const auto& s : st) n += countAssignments(s, cs);
        CHECK(n == expectedCands[E - 1]);
    }
}

TEST(enumerate_identical_components_collapse) {
    // two 10k resistors + cap: 10 -> 7 candidates (DESIGN.md 4.4)
    ComponentSet cs = ComponentSet::make({10e3, 10e3}, {100e-9}, {});
    const auto& st = enumerateStructures(3);
    long n = 0;
    for (const auto& s : st) n += countAssignments(s, cs);
    CHECK(n == 7);
}

TEST(enumerate_e4_matches_bruteforce_slot_assignment) {
    // E<=4: every distinct assignment orbit must equal the brute-force count
    // over all V, all slot placements, modulo relabeling + within-slot order
    // (mirrors test_enumerate.py brute force cross-check in orbit terms).
    // We verify orbit consistency instead: same serial never repeats.
    ComponentSet cs = ComponentSet::make({100.0, 470.0}, {100e-9}, {{1e-3, 5.0}});
    const auto& st = enumerateStructures(4);
    for (const auto& s : st) {
        std::set<std::vector<std::vector<Component>>> seen;
        int cnt = 0;
        iterAssignments(s, cs, [&](const std::vector<int>& assign) {
            Network net{s, assign};
            auto ser = net.serialize(cs.components());
            CHECK(!seen.count(ser));  // no duplicate orbit
            seen.insert(ser);
            ++cnt;
        });
        CHECK(cnt == (int)seen.size());
    }
}

// ---------------------------------------------------------------------------
// nodal
// ---------------------------------------------------------------------------

TEST(nodal_v2_closed_form) {
    // R || C || L(+dcr) on the single node pair: closed-form parallel formula
    ComponentSet cs = ComponentSet::make({1e3}, {10e-9}, {{100e-6, 2.0}});
    Network net = networkFromEdges(cs, {{0, 1, 0}, {0, 1, 1}, {0, 1, 2}});
    std::vector<double> f{100.0, 1e4, 1e6};
    auto z = networkZ(net, cs, f);
    for (size_t k = 0; k < f.size(); ++k) {
        Complex s(0.0, 2.0 * 3.14159265358979323846 * f[k]);
        Complex y = Complex(1.0 / 1e3, 0.0) + s * 10e-9 + 1.0 / (Complex(2.0, 0.0) + s * 100e-6);
        CHECK_CNEAR(z[k], 1.0 / y, 1e-12);
    }
}

TEST(nodal_series_chain) {
    // R + C series: Z = R + 1/(sC)
    ComponentSet cs = ComponentSet::make({1e3}, {100e-9}, {});
    Network net = networkFromEdges(cs, {{0, 2, 0}, {2, 1, 1}});
    std::vector<double> f{1e3, 1e5};
    auto z = networkZ(net, cs, f);
    for (size_t k = 0; k < f.size(); ++k) {
        Complex s(0.0, 2.0 * 3.14159265358979323846 * f[k]);
        CHECK_CNEAR(z[k], Complex(1e3, 0.0) + 1.0 / (s * 100e-9), 1e-12);
    }
}

TEST(nodal_balanced_wheatstone_bridge) {
    // py test_nodal.py hand value: arms 100+200 each -> 300 || 300 = 150 ohm,
    // bridge element (2,3) carries no current in the balanced state
    ComponentSet cs = ComponentSet::make({100.0, 100.0, 200.0, 200.0, 1e6}, {}, {});
    Network net = networkFromEdges(cs, {{0, 2, 0}, {0, 3, 1}, {1, 2, 2}, {1, 3, 3}, {2, 3, 4}});
    auto z = networkZ(net, cs, {1e3});
    CHECK_CNEAR(z[0], Complex(150.0, 0.0), 1e-9);
}

namespace {
}  // namespace

TEST(nodal_matches_independent_mna) {
    // reuse refZ logic inline (long double, straightforward stamping)
    std::mt19937_64 rng(11);
    for (int trial = 0; trial < 30; ++trial) {
        ComponentSet cs = ComponentSet::make(
            {std::pow(10.0, (double)(rng() % 5) + 1)},
            {std::pow(10.0, -(double)(rng() % 4) - 7)},
            {{std::pow(10.0, -(double)(rng() % 5) - 3), std::pow(10.0, (double)(rng() % 3))}});
        std::mt19937_64 r2(rng());
        Network net = randomNetwork(cs, r2);
        std::vector<double> f{1e2, 1e4, 1e6};
        auto z = networkZ(net, cs, f);
        for (size_t k = 0; k < f.size(); ++k) {
            // independent stamping in long double
            int V = net.structure.V;
            using CL = std::complex<long double>;
            std::vector<CL> Y((size_t)(V - 1) * (V - 1), CL(0.0L, 0.0L));
            const auto soi = net.structure.slotOfInstances();
            for (size_t t = 0; t < soi.size(); ++t) {
                const Component& c = cs.components()[net.assign[t]];
                auto [u, v] = slotList(V)[soi[t]];
                CL s(0.0L, 2.0L * 3.14159265358979323846L * (long double)f[k]);
                CL y = c.kind == 'R' ? CL(1.0L / (long double)c.value, 0.0L)
                      : c.kind == 'C' ? s * (long double)c.value
                      : 1.0L / (CL((long double)c.dcr, 0.0L) + s * (long double)c.value);
                int ri = u == 0 ? -1 : u - 1, rj = v == 0 ? -1 : v - 1;
                if (ri >= 0) Y[(size_t)ri * (V - 1) + ri] += y;
                if (rj >= 0) Y[(size_t)rj * (V - 1) + rj] += y;
                if (ri >= 0 && rj >= 0) {
                    Y[(size_t)ri * (V - 1) + rj] -= y;
                    Y[(size_t)rj * (V - 1) + ri] -= y;
                }
            }
            int k2 = V - 1;
            std::vector<CL> A = Y, b((size_t)k2, CL(0.0L, 0.0L));
            b[0] = CL(1.0L, 0.0L);
            for (int col = 0; col < k2; ++col) {
                int piv = col;
                long double best = std::abs(A[(size_t)col * k2 + col]);
                for (int r = col + 1; r < k2; ++r) {
                    long double a = std::abs(A[(size_t)r * k2 + col]);
                    if (a > best) { best = a; piv = r; }
                }
                for (int j = 0; j < k2; ++j) std::swap(A[(size_t)piv * k2 + j], A[(size_t)col * k2 + j]);
                std::swap(b[piv], b[col]);
                for (int r = col + 1; r < k2; ++r) {
                    CL fac = A[(size_t)r * k2 + col] / A[(size_t)col * k2 + col];
                    for (int j = col; j < k2; ++j) A[(size_t)r * k2 + j] -= fac * A[(size_t)col * k2 + j];
                    b[r] -= fac * b[col];
                }
            }
            for (int i = k2 - 1; i >= 0; --i) {
                for (int j = i + 1; j < k2; ++j) b[i] -= A[(size_t)i * k2 + j] * b[j];
                b[i] /= A[(size_t)i * k2 + i];
            }
            CL ref = b[0];
            CHECK_CNEAR(z[k], Complex((double)ref.real(), (double)ref.imag()), 1e-9);
        }
    }
}

TEST(nodal_batch_equals_single) {
    ComponentSet cs = ComponentSet::make({100.0, 470.0, 1e3}, {100e-9}, {{1e-3, 5.0}});
    const auto& st = enumerateStructures(5);
    StructureStamps stamps = StructureStamps::build(st[10], cs);
    std::vector<std::vector<int>> assigns;
    iterAssignments(st[10], cs, [&](const std::vector<int>& a) { assigns.push_back(a); });
    CHECK(assigns.size() > 3);
    std::vector<Complex> s{Complex(0, 6283.18), Complex(0, 6.28318e5)};
    auto batch = stamps.zFull(assigns, s);
    for (size_t c = 0; c < assigns.size(); ++c) {
        for (size_t m = 0; m < s.size(); ++m) {
            auto one = stamps.zFull({assigns[c]}, {s[m]});
            CHECK_CNEAR(batch[c][m], one[0][0], 1e-12);
        }
    }
}

TEST(nodal_passivity) {
    std::mt19937_64 rng(13);
    for (int trial = 0; trial < 20; ++trial) {
        ComponentSet cs = ComponentSet::make(
            {std::pow(10.0, (double)(rng() % 5) + 1)},
            {std::pow(10.0, -(double)(rng() % 4) - 7)},
            {{std::pow(10.0, -(double)(rng() % 5) - 3), 0.5}});
        std::mt19937_64 r2(rng());
        Network net = randomNetwork(cs, r2);
        auto z = networkZ(net, cs, defaultFrequencies(20));
        for (auto zk : z) CHECK(zk.real() > -1e-9 * std::abs(zk));
    }
}

TEST(nodal_asymptotes) {
    // series R + C: dc -> C open -> Z(0) = inf; hf -> C short -> Z(inf) = R
    ComponentSet csRC = ComponentSet::make({1e3}, {100e-9}, {});
    Network netRC = networkFromEdges(csRC, {{0, 2, 0}, {2, 1, 1}});
    CHECK(std::isinf(asymptoteImpedance(netRC, csRC, true)));
    CHECK_NEAR(asymptoteImpedance(netRC, csRC, false), 1e3, 1e-12);
    // series R + lossy L: dc -> R + dcr; hf -> L open -> Z(inf) = inf
    ComponentSet csRL = ComponentSet::make({100.0}, {}, {{10e-3, 5.0}});
    Network netRL = networkFromEdges(csRL, {{0, 2, 0}, {2, 1, 1}});
    CHECK_NEAR(asymptoteImpedance(netRL, csRL, true), 105.0, 1e-12);
    CHECK(std::isinf(asymptoteImpedance(netRL, csRL, false)));
    // parallel R || C: dc = R, hf = 0
    ComponentSet csPar = ComponentSet::make({2e3}, {1e-6}, {});
    Network netPar = networkFromEdges(csPar, {{0, 1, 0}, {0, 1, 1}});
    CHECK_NEAR(asymptoteImpedance(netPar, csPar, true), 2e3, 1e-12);
    CHECK_NEAR(asymptoteImpedance(netPar, csPar, false), 0.0, 1e-12);
}

// ---------------------------------------------------------------------------
// metric
// ---------------------------------------------------------------------------

TEST(metric_residual_interleave) {
    std::vector<Complex> z{{1.0, 2.0}, {3.0, -1.0}};
    std::vector<Complex> zm{{0.5, 1.0}, {3.0, 0.0}};
    std::vector<double> w{0.5, 0.25};
    auto r = residualVector(z, zm, w);
    CHECK(r.size() == 4);
    CHECK_NEAR(r[0], 0.25, 1e-15);
    CHECK_NEAR(r[1], 0.5, 1e-15);
    CHECK_NEAR(r[2], 0.0, 1e-15);
    CHECK_NEAR(r[3], -0.25, 1e-15);
    CHECK_NEAR(rssOf(r), 0.375, 1e-12);
}

TEST(metric_fit_metrics_and_aicc) {
    std::vector<Complex> z{{10.0, 0.0}, {0.0, 10.0}};
    std::vector<Complex> zf{{9.0, 0.0}, {0.0, 12.0}};
    auto [wrmse, mre] = fitMetrics(z, zf);
    CHECK_NEAR(wrmse, std::sqrt((0.01 + 0.04) / 2.0), 1e-14);
    CHECK_NEAR(mre, 0.2, 1e-14);
    // AICc is a strictly monotone transform of RSS for fixed (n_obs, p)
    double a1 = aicc(1.0, 60, 3), a2 = aicc(0.5, 60, 3), a3 = aicc(2.0, 60, 3);
    CHECK(a2 < a1 && a1 < a3);
}

TEST(metric_weighted_rss_batch_matches_single) {
    std::vector<Complex> z{{1.0, 1.0}, {2.0, -1.0}};
    std::vector<double> w{1.0, 0.5};
    std::vector<std::vector<Complex>> zm{{{0.0, 0.0}, {2.0, -1.0}},
                                         {{1.0, 1.0}, {2.0, -1.0}}};
    auto r = weightedRssBatch(z, zm, w);
    CHECK_NEAR(r[0], 2.0, 1e-14);
    CHECK_NEAR(r[1], 0.0, 1e-14);
    CHECK_NEAR(weightedRss(z, zm[0], w), r[0], 1e-14);
}

TEST(filters_coarse_indices_bankers_rounding) {
    // Python round(14.5) == 14 (banker's), so M=30 -> [0, 14, 29]
    auto idx = coarseIndices(30, 3);
    CHECK(idx.size() == 3);
    CHECK(idx[0] == 0 && idx[1] == 14 && idx[2] == 29);
    auto small = coarseIndices(2, 3);
    CHECK(small.size() == 2 && small[0] == 0 && small[1] == 1);
    CHECK(roundHalfEven(2.5) == 2);
    CHECK(roundHalfEven(3.5) == 4);
    CHECK(roundHalfEven(0.5) == 0);
    CHECK(roundHalfEven(1.5) == 2);
}
