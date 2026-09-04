// v2 suites: unified adjacency-matrix output (OUTPUT_FORMAT.md), unified
// measurement + count.txt input (INPUT_FORMAT.md), and the exact-N device
// prior — mirrors tests/test_adjacency.py, test_iofmt.py and the exact-N
// cases of test_end_to_end.py.

#include "framework.hpp"

#include "adjacency.hpp"
#include "iofmt.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace rlctest {

static TreePtr T_leaf(char k) { return Tree::makeLeaf(k); }
static TreePtr T_node(NK k, std::vector<TreePtr> kids) {
    return Tree::makeNode(k, std::move(kids));
}

// ---------------------------------------------------------------------------
// adjacency: structure, multiedges with dcr, independent MNA cross-check
// ---------------------------------------------------------------------------

static Complex edgeImpedance(const Edge& e, Complex s) {
    if (e.type == 'R') return Complex(e.parameter, 0.0);
    if (e.type == 'L') return e.dcr + s * e.parameter;
    return Complex(1.0, 0.0) / (s * e.parameter);
}

static std::vector<Complex> zFromAdjacency(const Adjacency& adj,
                                           const std::vector<double>& f) {
    const int V = adj.V();
    std::vector<Complex> out(f.size());
    for (size_t m = 0; m < f.size(); ++m) {
        Complex s(0.0, 2.0 * M_PI * f[m]);
        std::vector<std::vector<Complex>> Y(V, std::vector<Complex>(V));
        for (const auto& [i, j, edges] : adj.occupied()) {
            for (const auto& e : *edges) {
                Complex y = Complex(1.0, 0.0) / edgeImpedance(e, s);
                Y[i][i] += y;
                Y[j][j] += y;
                Y[i][j] -= y;
                Y[j][i] -= y;
            }
        }
        // ground terminal 1, inject 1 A into terminal 0
        std::vector<int> keep;
        for (int n = 0; n < V; ++n)
            if (n != 1) keep.push_back(n);
        int n = V - 1;
        std::vector<std::vector<Complex>> Yr(n, std::vector<Complex>(n));
        std::vector<Complex> rhs(n);
        for (int a = 0; a < n; ++a)
            for (int b = 0; b < n; ++b) Yr[a][b] = Y[keep[a]][keep[b]];
        rhs[0] = Complex(1.0, 0.0);
        for (int col = 0; col < n; ++col) {
            int piv = col;
            for (int r = col + 1; r < n; ++r)
                if (std::abs(Yr[r][col]) > std::abs(Yr[piv][col])) piv = r;
            std::swap(Yr[piv], Yr[col]);
            std::swap(rhs[piv], rhs[col]);
            for (int r = col + 1; r < n; ++r) {
                Complex fac = Yr[r][col] / Yr[col][col];
                for (int c2 = col; c2 < n; ++c2) Yr[r][c2] -= fac * Yr[col][c2];
                rhs[r] -= fac * rhs[col];
            }
        }
        std::vector<Complex> Vv(n);
        for (int r = n - 1; r >= 0; --r) {
            Complex acc = rhs[r];
            for (int c2 = r + 1; c2 < n; ++c2) acc -= Yr[r][c2] * Vv[c2];
            Vv[r] = acc / Yr[r][r];
        }
        out[m] = Vv[0];
    }
    return out;
}

void suiteAdjacency(TestCtx& t) {
    t.suite = "adjacency";

    t.begin("bare_leaf");
    {
        Adjacency adj = treeToAdjacency(T_leaf('R'), {2.0});
        t.check(adj.V() == 2, "V = 2");
        auto& e = adj.slot(0, 1);
        t.check(e.size() == 1 && e[0].type == 'R' && e[0].dcr == 0.0, "one R edge");
        t.checkClose(e[0].parameter, 100.0, 1e-12, "R = 100");
        t.check(adj.nEdges() == 1, "1 edge");
        t.end();
    }
    t.begin("bare_inductor_carries_dcr");
    {
        Adjacency adj = treeToAdjacency(T_leaf('L'), {std::log10(1e-3), std::log10(5.0)});
        auto& e = adj.slot(0, 1);
        t.check(e.size() == 1 && e[0].type == 'L', "one L edge");
        t.checkClose(e[0].parameter, 1e-3, 1e-9, "L value");
        t.checkClose(e[0].dcr, 5.0, 1e-9, "fitted dcr");
        t.end();
    }
    t.begin("series_rl_chain");
    {
        auto tr = T_node(NK::Ser, {T_leaf('R'), T_leaf('L')});
        std::vector<double> th{std::log10(1e-3), std::log10(0.5), 2.0};
        Adjacency adj = treeToAdjacency(tr, th);
        t.check(adj.V() == 3, "chain V = 3");
        auto& eL = adj.slot(0, 2);
        t.check(eL.size() == 1 && eL[0].type == 'L', "L on (0,2)");
        t.checkClose(eL[0].dcr, 0.5, 1e-9, "L dcr");
        auto& eR = adj.slot(1, 2);
        t.check(eR.size() == 1 && eR[0].type == 'R', "R on (1,2)");
        t.checkClose(eR[0].parameter, 100.0, 1e-9, "R value");
        t.end();
    }
    t.begin("parallel_ll_multiedge_with_dcr");
    {
        auto tr = T_node(NK::Par, {T_leaf('L'), T_leaf('L')});
        std::vector<double> theta{std::log10(1e-3), std::log10(0.5),
                                  std::log10(1e-5), std::log10(10.0)};
        Adjacency adj = treeToAdjacency(tr, theta);
        t.check(adj.V() == 2, "V = 2");
        auto& es = adj.slot(0, 1);
        t.check(es.size() == 2, "multi-edge (two parallel inductors)");
        t.check(es[0].type == 'L' && es[1].type == 'L', "both L edges");
        t.checkClose(es[0].dcr, 0.5, 1e-9, "L1 dcr");
        t.checkClose(es[1].dcr, 10.0, 1e-9, "L2 dcr");
        auto f = geomspace(1e2, 1e6, 25);
        auto zg = zFromAdjacency(adj, f);
        std::vector<Complex> zt(f.size());
        evalThetaFreq(tr, theta, f.data(), f.size(), zt.data());
        double mx = 0.0;
        for (size_t k = 0; k < f.size(); ++k)
            mx = std::max(mx, std::abs(zg[k] - zt[k]) / std::max(std::abs(zt[k]), 1e-300));
        t.check(mx < 1e-9, "MNA cross-check on the multi-edge graph");
        t.end();
    }
    t.begin("theta_length_validated");
    {
        auto tr = T_node(NK::Ser, {T_leaf('R'), T_leaf('L')});
        bool threw = false;
        try { treeToAdjacency(tr, {1.0}); } catch (const std::exception&) { threw = true; }
        t.check(threw, "SER(R,L) needs 3 parameters");
        t.end();
    }
    t.begin("invariants_over_duts");
    {
        for (const auto& dut : makeDuts()) {
            Adjacency adj = treeToAdjacency(dut.tree, dut.theta());
            t.check(adj.V() == 2 + nChainNodes(dut.tree), dut.name + " V");
            t.check(adj.nEdges() == nLeaves(dut.tree), dut.name + " edge conservation");
            // deterministic emission
            Adjacency adj2 = treeToAdjacency(dut.tree, dut.theta());
            bool same = true;
            for (const auto& [i, j, e1] : adj.occupied()) {
                const auto& e2 = adj2.slot(i, j);
                if (e1->size() != e2.size()) same = false;
                for (size_t k = 0; k < e1->size(); ++k)
                    if ((*e1)[k].type != e2[k].type ||
                        (*e1)[k].parameter != e2[k].parameter ||
                        (*e1)[k].dcr != e2[k].dcr)
                        same = false;
            }
            t.check(same, dut.name + " deterministic");
            // Z cross-validation (independent nodal solver)
            auto f = geomspace(1e1, 1e7, 30);
            auto zg = zFromAdjacency(adj, f);
            std::vector<Complex> zt(f.size());
            evalThetaFreq(dut.tree, dut.theta(), f.data(), f.size(), zt.data());
            double mx = 0.0;
            for (size_t k = 0; k < f.size(); ++k)
                mx = std::max(mx, std::abs(zg[k] - zt[k]) / std::max(std::abs(zt[k]), 1e-300));
            t.check(mx < 1e-9, dut.name + " Z cross-validation");
        }
        t.end();
    }
    t.begin("format_block");
    {
        auto tr = T_node(NK::Ser, {T_leaf('R'), T_leaf('L')});
        std::vector<double> th{std::log10(1e-3), std::log10(10.0), std::log10(100.0)};
        // canonical child order: L first, then R
        Adjacency adj = treeToAdjacency(tr, {std::log10(1e-3), std::log10(10.0), 2.0});
        std::string text = adj.formatBlock("1");
        t.check(text.find("adjacency[1] V=3 (ports 0,1):") == 0, "header");
        t.check(text.find("L 1.000e-03 dcr 1.000e+01") != std::string::npos, "L with dcr");
        t.check(text.find("R 1.000e+02") != std::string::npos, "R edge");
        t.check(text.find("dcr") != std::string::npos, "dcr printed");
        (void)th;
        t.end();
    }
}

// ---------------------------------------------------------------------------
// iofmt: measurements.txt + count.txt (INPUT_FORMAT.md sec 1 / 2.1)
// ---------------------------------------------------------------------------

void suiteIofmt(TestCtx& t) {
    t.suite = "iofmt";

    t.begin("measurements_spec_example");
    {
        Measurements ms = parseMeasurements(
            "# measurements.txt\n2\n1.0e+03  9.98e+02  -1.2e-01\n1.0e+04  6.13e+02  -4.88e+02\n");
        t.check(ms.f.size() == 2, "two points");
        t.checkClose(ms.f[0], 1e3, 0.0, "f0");
        t.checkClose(ms.z[1].real(), 613.0, 0.0, "z1 re");
        t.checkClose(ms.z[1].imag(), -488.0, 0.0, "z1 im");
        t.end();
    }
    t.begin("measurements_round_trip_bit_exact");
    {
        Rng rng(0);
        std::vector<double> f = geomspace(10.0, 1e7, 30);
        std::vector<Complex> z(30);
        for (size_t k = 0; k < 30; ++k) z[k] = Complex(rng.normal(), rng.normal());
        Measurements ms = parseMeasurements(formatMeasurements(f, z));
        t.check(ms.f.size() == f.size(), "same size");
        bool same = true;
        for (size_t k = 0; k < f.size(); ++k) {
            // %.17g round-trips doubles bit-for-bit
            if (ms.f[k] != f[k] || ms.z[k] != z[k]) same = false;
        }
        t.check(same, "bit-for-bit round trip");
        t.end();
    }
    t.begin("measurements_validation_errors");
    {
        const char* bad[] = {
            "",                    // empty
            "x\n1 2 3\n",          // n not an integer
            "0\n",                 // n must be positive
            "3\n1 2 3\n4 5 6\n",   // line count mismatch
            "1\n1 2\n",            // too few fields
            "1\n1 two 3\n",        // non-numeric
            "1\n0 1 2\n",          // frequency <= 0
            "1\ninf 1 2\n",        // non-finite
            "1\n1 2 3 4\n",        // too many fields
        };
        for (const char* b : bad) {
            bool threw = false;
            try { parseMeasurements(b); } catch (const std::exception&) { threw = true; }
            t.check(threw, std::string("rejects: ") + b);
        }
        t.end();
    }
    t.begin("count_spec_example");
    {
        t.check(parseCount("# count.txt\n3\n") == 3, "count with comment");
        t.check(formatCount(5) == "5\n", "format");
        t.check(parseCount(formatCount(5)) == 5, "round trip");
        const char* bad[] = {"", "3\n4\n", "three\n", "0\n", "-2\n", "2.5\n"};
        for (const char* b : bad) {
            bool threw = false;
            try { parseCount(b); } catch (const std::exception&) { threw = true; }
            t.check(threw, std::string("rejects: ") + b);
        }
        bool threw = false;
        try { formatCount(0); } catch (const std::exception&) { threw = true; }
        t.check(threw, "format validates too");
        t.end();
    }
}

// ---------------------------------------------------------------------------
// exact-N prior (Config::exactN / count.txt)
// ---------------------------------------------------------------------------

void suiteExactN(TestCtx& t) {
    t.suite = "exact_n";
    auto duts = makeDuts();
    auto byName = [&](const char* n) -> const DUT* {
        for (const auto& d : duts)
            if (d.name == n) return &d;
        return nullptr;
    };
    auto paramSkip = [](const std::string& n) { return n == "dut10_ser_R_par_LL"; };

    t.begin("right_n_recovers_truth");
    {
        for (const char* name : {"dut1b_L", "dut3a_par_RC", "dut7_tank",
                                 "dut9_par_LL", "dut10_ser_R_par_LL"}) {
            const DUT* dut = byName(name);
            int n = nLeaves(dut->tree);
            Measurement ms = measure(*dut, nullptr, 0.0, 0);
            Config cfg;
            cfg.exactN = n;
            IdentifyResult res = identify(ms.f, ms.z, nullptr, &cfg);
            t.check(res.nLibrary == (int)TopologyLibrary::ofSize(n, 2).size(),
                    std::string(name) + " library = single layer");
            bool allN = !res.classes.empty();
            for (const auto& cls : res.classes)
                if (nLeaves(cls.representative.tree) != n) allN = false;
            t.check(allN, std::string(name) + " every candidate has n devices");
            // top-1 matches the truth directly or via an equivalent
            // realization (T2)
            double perr;
            std::string status = classifyDut(*dut, res, ms.f, 1e-6, perr);
            t.check(status != "MISS", std::string(name) + " top-1 matches (" + status + ")");
            if (status == "EXACT" && !paramSkip(name))
                t.check(perr < 1e-4, std::string(name) + " param recovery");
        }
        t.end();
    }
    t.begin("wrong_n_returns_valid_layer");
    {
        const DUT* dut = byName("dut7_tank");  // 3 devices
        Measurement ms = measure(*dut, nullptr, 0.0, 0);
        Config cfg;
        cfg.exactN = 2;
        IdentifyResult res = identify(ms.f, ms.z, nullptr, &cfg);
        bool allN = !res.classes.empty();
        for (const auto& cls : res.classes)
            if (nLeaves(cls.representative.tree) != 2) allN = false;
        t.check(allN, "with a wrong prior every candidate still has 2 devices");
        t.end();
    }
    t.begin("rejects_bad_values");
    {
        Config cfg;
        cfg.exactN = 0;
        bool threw = false;
        try { identify({10.0}, {Complex(1.0, 0.0)}, nullptr, &cfg); }
        catch (const std::exception&) { threw = true; }
        t.check(threw, "exactN = 0 rejected");
        t.end();
    }
}

}  // namespace rlctest
