// Core test suites: circuits, library, engine A, engine B, pruning, selector
// and the 12-DUT end-to-end mirrors of the Python pytest suite.

#include "framework.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace rlctest {

// ===========================================================================
// circuits: normalization (R1-R3), evaluation, AD Jacobian
// ===========================================================================

static TreePtr T_leaf(char k) { return Tree::makeLeaf(k); }
static TreePtr T_node(NK k, std::vector<TreePtr> kids) {
    return Tree::makeNode(k, std::move(kids));
}

void suiteCircuits(TestCtx& t) {
    t.suite = "circuits";

    // ---- normalization -----------------------------------------------------
    t.begin("r1_flattens_same_kind_nesting");
    {
        auto t1 = T_node(NK::Ser, {T_leaf('R'), T_node(NK::Ser, {T_leaf('L'), T_leaf('C')})});
        auto t2 = T_node(NK::Ser, {T_leaf('R'), T_leaf('L'), T_leaf('C')});
        t.check(canonical(normalize(t1)) == canonical(t2), "R1 flatten");
        t.end();
    }
    t.begin("r2_merges_duplicate_leaf_kinds");
    {
        auto t1 = T_node(NK::Ser, {T_leaf('R'), T_leaf('R'), T_leaf('L')});
        auto t2 = T_node(NK::Ser, {T_leaf('R'), T_leaf('L')});
        t.check(canonical(normalize(t1)) == canonical(t2), "R2 merge");
        t.end();
    }
    t.begin("r3_child_order_irrelevant");
    {
        auto t1 = T_node(NK::Ser, {T_leaf('R'), T_leaf('L')});
        auto t2 = T_node(NK::Ser, {T_leaf('L'), T_leaf('R')});
        t.check(canonical(t1) == canonical(t2), "R3 order");
        t.end();
    }
    t.begin("normalize_idempotent");
    {
        auto tr = T_node(NK::Par, {T_leaf('C'), T_node(NK::Ser, {T_leaf('R'), T_leaf('L')})});
        t.check(canonical(normalize(normalize(tr))) == canonical(normalize(tr)), "idempotent");
        t.end();
    }
    t.begin("single_child_collapses");
    {
        auto tr = normalize(T_node(NK::Ser, {T_leaf('R'), T_leaf('R')}));
        t.check(tr->isLeaf && tr->elem == 'R', "collapse to leaf");
        t.end();
    }

    // ---- evaluation against hand values ------------------------------------
    t.begin("single_elements");
    {
        double f0 = 1e3;
        Complex s(0.0, 2.0 * M_PI * f0);
        Complex zr, zl, zc;
        evalTheta(T_leaf('R'), {2.0}, &s, 1, &zr);
        evalTheta(T_leaf('L'), {-3.0}, &s, 1, &zl);
        evalTheta(T_leaf('C'), {-6.0}, &s, 1, &zc);
        t.checkClose(zr.real(), 100.0, 1e-12, "R value");
        t.checkClose(zl.imag(), 2.0 * M_PI * 1e3 * 1e-3, 1e-12, "L value");
        t.checkClose(std::abs(zc), 1.0 / (2.0 * M_PI * 1e3 * 1e-6), 1e-12, "C value");
        t.checkClose(zc.imag(), -1.0 / (2.0 * M_PI * 1e3 * 1e-6), 1e-12, "C imag");
        t.end();
    }
    t.begin("series_parallel_hand_value");
    {
        double w = 1e5;
        Complex s(0.0, w);
        double R = 50.0, L = 1e-3, C = 1e-9;
        Assembled a = assemble(NK::Ser,
                               {Assembled{T_leaf('R'), {R}},
                                assemble(NK::Par, {Assembled{T_leaf('L'), {L}},
                                                   Assembled{T_leaf('C'), {C}}})});
        std::vector<double> theta(a.values.size());
        for (size_t i = 0; i < a.values.size(); ++i) theta[i] = std::log10(a.values[i]);
        Complex z;
        evalTheta(a.tree, theta, &s, 1, &z);
        Complex zl(0.0, w * L), zc(0.0, 0.0);
        zc = Complex(1.0, 0.0) / (s * C);
        Complex expected = R + zl * zc / (zl + zc);
        t.checkClose(z.real(), expected.real(), 1e-12, "SP real");
        t.checkClose(z.imag(), expected.imag(), 1e-12, "SP imag");
        t.end();
    }
    t.begin("to_string_shapes");
    {
        Assembled a = assemble(NK::Ser,
                               {Assembled{T_leaf('R'), {100.0}},
                                assemble(NK::Par, {Assembled{T_leaf('C'), {1e-8}},
                                                   Assembled{T_leaf('R'), {1e3}}})});
        std::vector<double> theta(a.values.size());
        for (size_t i = 0; i < a.values.size(); ++i) theta[i] = std::log10(a.values[i]);
        std::string text = toString(a.tree, &theta);
        t.check(text.find("R(100)") != std::string::npos, "R(100) present");
        t.check(text.find("||") != std::string::npos, "|| present");
        t.check(text.find("+") != std::string::npos, "+ present");
        t.check(fmtEng(1e-8).find("10n") != std::string::npos, "fmtEng 1e-8");
        t.end();
    }

    // ---- full-library cross checks -----------------------------------------
    // every canonical topology with 1..6 elements, two random draws each:
    // (a) tree evaluation vs independent nodal analysis (MNA)
    // (b) forward-AD Jacobian vs central finite differences
    warmLibraryCache(6, 2);
    const std::vector<TreePtr>& lib = TopologyLibrary::get(6, 2);
    auto freqs = defaultFrequencies();
    const size_t M = freqs.size();
    std::vector<Complex> s(M);
    for (size_t k = 0; k < M; ++k) s[k] = Complex(0.0, 1.0) * (2.0 * M_PI * freqs[k]);

    int total = (int)lib.size() * 2;
    std::atomic<int> fdSkipped{0};
    auto results = runParallel(total, 8, [&](int i) {
        const TreePtr& tree = lib[i / 2];
        int draw = i % 2;
        Rng rng(7000 + i);
        auto kinds = leafKinds(tree);
        std::vector<double> theta(kinds.size());
        std::vector<double> values(kinds.size());
        for (size_t j = 0; j < kinds.size(); ++j) {
            auto b = kindBounds(kinds[j]);
            theta[j] = b.first + 0.5 + rng.uniform01() * (b.second - b.first - 1.0);
            values[j] = std::pow(10.0, theta[j]);
        }
        CaseResult r;
        r.index = i;
        r.name = "mna_vs_eval[" + canonical(tree) + "/" + std::to_string(draw) + "]";
        double worstMna = 0.0, worstFd = 0.0;
        // (a) MNA cross-check on a 7-point sub-grid.  Near series resonance
        // |Z| can vanish, so the comparison uses a mixed relative/absolute
        // criterion scaled by the network's own impedance scale.
        double zScale = 0.0;
        std::vector<Complex> zSample;
        for (int k = 0; k < (int)M; k += (int)M / 6) {
            Complex zTree;
            evalTheta(tree, theta, &s[k], 1, &zTree);
            zSample.push_back(zTree);
            zScale = std::max(zScale, std::abs(zTree));
        }
        int kk = 0;
        for (int k = 0; k < (int)M; k += (int)M / 6, ++kk) {
            Complex zMna = mnaImpedance(tree, values, s[k]);
            double diff = std::abs(zSample[kk] - zMna);
            // 1e-6 relative covers MNA rounding when the admittance ratio is
            // extreme (10+ decades); structural bugs produce O(1) errors
            double limit = std::max(1e-6 * std::abs(zSample[kk]), 1e-11 * zScale);
            worstMna = std::max(worstMna, diff / std::max(limit, 1e-300));
        }
        if (!(worstMna < 1.0)) {
            r.ok = false;
            char buf[160];
            std::snprintf(buf, sizeof(buf), "MNA mismatch %.3g", worstMna);
            r.detail = buf;
        }
        // (b) Adaptive-step Richardson 5-point finite-difference check.
        // Steps shrink 1e-3 -> 1e-5 until two consecutive estimates agree;
        // parameters where even the finest step does not stabilize
        // (super-high-Q resonances of extreme random draws) are skipped and
        // counted.  Rule-level AD bugs (wrong sign, missing ln 10, wrong
        // recursion) produce O(1) errors at every step size.
        {
            std::vector<Complex> Z(M), J(kinds.size() * M);
            evalJac(tree, theta, s.data(), M, Z.data(), J.data());
            double jacScale = 0.0;
            for (size_t iP = 0; iP < kinds.size(); ++iP)
                for (size_t k = 0; k < M; ++k)
                    jacScale = std::max(jacScale, std::abs(J[iP * M + k]));
            const double steps[] = {1e-3, 3e-4, 1e-4, 3e-5, 1e-5};
            for (size_t iP = 0; iP < kinds.size(); ++iP) {
                double adMax = 0.0;
                for (size_t k = 0; k < M; ++k)
                    adMax = std::max(adMax, std::abs(J[iP * M + k]));
                if (adMax < 1e-10 * jacScale) continue;  // invisible element
                std::vector<double> base = theta;
                auto evalAt = [&](double shift, std::vector<Complex>& outv) {
                    std::vector<double> tv = base;
                    tv[iP] += shift;
                    evalTheta(tree, tv, s.data(), M, outv.data());
                };
                std::vector<Complex> p1(M), p2(M), m1(M), m2(M);
                auto richardsonAt = [&](double hh) {
                    evalAt(hh, p1);
                    evalAt(2.0 * hh, p2);
                    evalAt(-hh, m1);
                    evalAt(-2.0 * hh, m2);
                    double fdMax = 0.0, diff = 0.0;
                    for (size_t k = 0; k < M; ++k) {
                        Complex fd = (m2[k] - Complex(8.0, 0.0) * m1[k] +
                                      Complex(8.0, 0.0) * p1[k] - p2[k]) /
                                     (Complex(12.0 * hh, 0.0));
                        fdMax = std::max(fdMax, std::abs(fd));
                        diff = std::max(diff, std::abs(J[iP * M + k] - fd));
                    }
                    double denom = std::max(std::max(fdMax, adMax), 1e-300);
                    return diff / denom;
                };
                double prev = richardsonAt(steps[0]);
                double best = prev;
                bool converged = false;
                for (int si = 1; si < 5 && !converged; ++si) {
                    double cur = richardsonAt(steps[si]);
                    if (std::fabs(cur - prev) < 0.02 * std::max(cur, 1e-300) + 1e-6) {
                        converged = true;
                        best = cur;
                    } else {
                        best = std::min(best, cur);
                        prev = cur;
                    }
                }
                if (!converged) {
                    ++fdSkipped;  // FD-untestable parameter (extreme draw)
                    continue;
                }
                worstFd = std::max(worstFd, best);
            }
            if (!(worstFd < 1e-2)) {
                r.ok = false;
                char buf[160];
                std::snprintf(buf, sizeof(buf), "FD mismatch %.3g", worstFd);
                r.detail += buf;
            }
        }
        return r;
    });
    for (auto& r : results) t.addCase(std::move(r));
    t.begin("fd_reference_notes");
    {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "FD-untestable parameters skipped: %d",
                      fdSkipped.load());
        t.check(true, buf);  // informational
        t.end();
    }
}

// ===========================================================================
// library: enumeration counts (locked by DESIGN.md section 4.2)
// ===========================================================================

void suiteLibrary(TestCtx& t) {
    t.suite = "library";
    const std::map<int, int> expected2{{1, 3}, {2, 6}, {3, 20}, {4, 36}, {5, 54}, {6, 78}};

    t.begin("counts_depth2");
    for (auto& kv : expected2) {
        t.check(TopologyLibrary::countOfSize(kv.first, 2) == kv.second,
                "count n=" + std::to_string(kv.first));
    }
    t.end();

    t.begin("counts_depth3_n4");
    t.check(TopologyLibrary::countOfSize(4, 3) == 90, "depth3 n=4 = 90");
    t.check(TopologyLibrary::countOfSize(1, 3) == 3, "depth3 n=1");
    t.check(TopologyLibrary::countOfSize(2, 3) == 6, "depth3 n=2");
    t.check(TopologyLibrary::countOfSize(3, 3) == 20, "depth3 n=3");
    t.end();

    t.begin("no_duplicate_canonicals");
    {
        auto& lib = TopologyLibrary::get(4);
        std::set<std::string> cans;
        for (auto& tr : lib) cans.insert(canonical(tr));
        t.check(cans.size() == lib.size(), "unique canonicals");
        t.end();
    }
    t.begin("library_cumulative");
    {
        auto& lib = TopologyLibrary::get(4);
        int expect = 3 + 6 + 20 + 36;
        t.check((int)lib.size() == expect, "cumulative n<=4 = 65");
        auto& lib6 = TopologyLibrary::get(6);
        t.check((int)lib6.size() == 3 + 6 + 20 + 36 + 54 + 78, "cumulative n<=6 = 197");
        t.end();
    }
    t.begin("idepth2_structure_shape");
    {
        // every library member respects R1 (alternating kinds), R2 (distinct
        // leaf kinds per node) and R3 (children sorted by canonical)
        auto checkTree = [](const TreePtr& tr, bool& ok) {
            std::function<void(const Tree*)> rec = [&](const Tree* p) {
                if (p->isLeaf) return;
                std::string prev;
                int mask = 0;
                for (const auto& c : p->kids) {
                    if (!c->isLeaf && c->kind == p->kind) ok = false;  // R1
                    if (c->isLeaf) {
                        int bit = 1 << (c->elem == 'R' ? 0 : c->elem == 'L' ? 1 : 2);
                        if (mask & bit) ok = false;  // R2
                        mask |= bit;
                    }
                    auto cs = canonical(c);
                    if (!prev.empty() && cs < prev) ok = false;  // R3
                    prev = cs;
                    rec(c.get());
                }
            };
            rec(tr.get());
        };
        bool ok = true;
        for (auto& tr : TopologyLibrary::get(6, 2)) checkTree(tr, ok);
        for (auto& tr : TopologyLibrary::get(4, 3)) checkTree(tr, ok);
        t.check(ok, "R1/R2/R3 respected by enumeration");
        t.end();
    }
}

// ===========================================================================
// engine A: fitting accuracy (noiseless + noisy), optimizer sanity
// ===========================================================================

static std::optional<Candidate> fitTrueTopology(const DUT& dut, double sigmaRel,
                                                uint64_t seed = 0) {
    Measurement ms = measure(dut, nullptr, sigmaRel, seed);
    const size_t m = ms.f.size();
    std::vector<Complex> s(m);
    for (size_t k = 0; k < m; ++k) s[k] = Complex(0.0, 1.0) * (2.0 * M_PI * ms.f[k]);
    std::vector<double> w = defaultWeights(ms.z);
    std::vector<double> wAng(m);
    for (size_t k = 0; k < m; ++k) wAng[k] = 2.0 * M_PI * ms.f[k];
    AsymptoticFeatures feat = extractAsymptotics(wAng, ms.z);
    StartHints hints = hintsFromFeatures(feat);
    std::vector<double> lb, ub;
    thetaBounds(dut.tree, lb, ub);
    std::vector<std::vector<double>> starts = heuristicStarts(dut.tree, &hints);
    FosterResult fc = fosterCandidates(wAng, ms.z, w, 4, 15);
    for (const auto& c : fc.candidates) {
        if (!c.skipped && c.canonicalStr() == canonical(dut.tree))
            starts.push_back(c.theta);
    }
    Rng rng(seed);
    auto lhs = lhsStarts(12, lb, ub, rng);
    for (auto& v : lhs) starts.push_back(std::move(v));
    return fitTopology(dut.tree, s, ms.z, w, starts, 0, 1e-12);
}

void suiteEngineA(TestCtx& t) {
    t.suite = "engine_a";
    auto duts = makeDuts();

    t.begin("noiseless_recovery_all_12");
    {
        int exact = 0;
        for (const auto& dut : duts) {
            auto cand = fitTrueTopology(dut, 0.0);
            bool ok = cand.has_value() && maxParamError(cand->theta, dut) < 1e-4 &&
                      cand->wrmse < 1e-10;
            if (ok) ++exact;
        }
        t.check(exact == 12, "12/12 noiseless param+fit recovery");
        t.end();
    }
    t.begin("noiseless_param_error_table");
    {
        // report-grade detail: record each DUT's parameter error
        for (const auto& dut : duts) {
            auto cand = fitTrueTopology(dut, 0.0);
            double perr = cand ? maxParamError(cand->theta, dut) : 1.0;
            t.check(perr < 1e-4, dut.name + " perr=" + std::to_string(perr));
        }
        t.end();
    }
    t.begin("noisy_recovery_0p5pc_all_12");
    {
        int okCount = 0;
        for (const auto& dut : duts) {
            auto cand = fitTrueTopology(dut, 0.005);
            if (cand && maxParamError(cand->theta, dut) < 0.02) ++okCount;
        }
        // the three DUTs locked by the Python suite must pass; the rest are
        // checked at the same 2% bar
        t.check(okCount >= 3, "at least the locked trio under 2%");
        t.check(okCount == 12, "all 12 under 2% (extended)");
        t.end();
    }
    t.begin("aicc_prefers_truth_on_noiseless");
    {
        const DUT* dut = nullptr;
        for (const auto& d : duts)
            if (d.name == "dut3a_par_RC") dut = &d;
        Measurement ms = measure(*dut, nullptr, 0.0, 0);
        const size_t m = ms.f.size();
        std::vector<Complex> s(m);
        for (size_t k = 0; k < m; ++k) s[k] = Complex(0.0, 1.0) * (2.0 * M_PI * ms.f[k]);
        std::vector<double> w = defaultWeights(ms.z);
        std::vector<double> wAng(m);
        for (size_t k = 0; k < m; ++k) wAng[k] = 2.0 * M_PI * ms.f[k];
        StartHints hints = hintsFromFeatures(extractAsymptotics(wAng, ms.z));
        auto trees = TopologyLibrary::get(3);
        EngineAConfig cfg;
        auto fits = fitLibrary(trees, s, ms.z, w, cfg, &hints, nullptr);
        std::stable_sort(fits.begin(), fits.end(),
                         [](const Candidate& a, const Candidate& b) {
                             return a.aiccVal < b.aiccVal;
                         });
        t.check(!fits.empty() && fits.front().canonicalStr() == canonical(dut->tree),
                "top AICc = truth");
        t.end();
    }

    // ---- optimizer sanity ---------------------------------------------------
    t.begin("lm_quadratic_bowl");
    {
        auto res = [](const std::vector<double>& x, std::vector<double>& out) {
            out = {x[0], x[1]};
        };
        auto jac = [](const std::vector<double>&, std::vector<double>& out) {
            out = {1.0, 0.0, 0.0, 1.0};
        };
        LMOpts o;
        o.ftol = o.xtol = o.gtol = 1e-14;
        LMOut out = lmFit(res, jac, {5.0, -3.0}, {-10.0, -10.0}, {10.0, 10.0}, o);
        t.check(out.rss < 1e-24, "bowl rss ~ 0");
        t.check(std::fabs(out.x[0]) < 1e-10 && std::fabs(out.x[1]) < 1e-10, "bowl x ~ 0");
        t.end();
    }
    t.begin("lm_bound_clipping");
    {
        auto res = [](const std::vector<double>& x, std::vector<double>& out) {
            out = {x[0] - 5.0};
        };
        auto jac = [](const std::vector<double>&, std::vector<double>& out) { out = {1.0}; };
        LMOpts o;
        LMOut out = lmFit(res, jac, {0.5}, {0.0}, {1.0}, o);
        t.check(std::fabs(out.x[0] - 1.0) < 1e-9, "clipped to upper bound");
        t.end();
    }
    t.begin("lm_ill_conditioned");
    {
        auto res = [](const std::vector<double>& x, std::vector<double>& out) {
            out = {1e3 * x[0], 1e-3 * x[1]};
        };
        auto jac = [](const std::vector<double>&, std::vector<double>& out) {
            out = {1e3, 0.0, 0.0, 1e-3};
        };
        LMOpts o;
        o.ftol = o.xtol = o.gtol = 1e-13;
        LMOut out = lmFit(res, jac, {2.0, 2.0}, {-10.0, -10.0}, {10.0, 10.0}, o);
        t.check(out.rss < 1e-20, "ill-cond rss ~ 0");
        t.end();
    }
    t.begin("lm_rank_deficient_jacobian");
    {
        auto res = [](const std::vector<double>& x, std::vector<double>& out) {
            out = {x[0] + 1.0, x[0] + 1.0};
        };
        auto jac = [](const std::vector<double>&, std::vector<double>& out) {
            out = {1.0, 0.0, 1.0, 0.0};
        };
        LMOut out = lmFit(res, jac, {2.0, 0.5}, {-10.0, -10.0}, {10.0, 10.0}, LMOpts{});
        t.check(out.rss < 1e-16, "rank-def rss ~ 0");
        t.check(std::fabs(out.x[1] - 0.5) < 1e-9, "flat parameter untouched");
        t.end();
    }
}

// ===========================================================================
// engine B: SK rational fit + Foster synthesis
// ===========================================================================

static FosterResult fitFoster(const DUT& dut, double sigmaRel = 0.0, int maxOrder = 4) {
    Measurement ms = measure(dut, nullptr, sigmaRel, 0);
    const size_t m = ms.f.size();
    std::vector<double> w(m);
    for (size_t k = 0; k < m; ++k) w[k] = 2.0 * M_PI * ms.f[k];
    return fosterCandidates(w, ms.z, defaultWeights(ms.z), maxOrder, 15);
}

void suiteEngineB(TestCtx& t) {
    t.suite = "engine_b";
    auto duts = makeDuts();
    auto byName = [&](const std::string& n) -> const DUT* {
        for (const auto& d : duts)
            if (d.name == n) return &d;
        return nullptr;
    };

    t.begin("rc_pole_recovery");
    {
        FosterResult fr = fitFoster(*byName("dut3a_par_RC"), 0.0);
        t.check(fr.zModel.poles.size() == 1, "one pole");
        double pTrue = -1.0 / (1e3 * 1e-8);
        if (fr.zModel.poles.size() == 1) {
            double rel = std::fabs(fr.zModel.poles[0].real() - pTrue) / std::fabs(pTrue);
            t.check(rel < 1e-3, "pole at -1/(RC)");
        }
        t.end();
    }
    t.begin("tank_pole_pair");
    {
        FosterResult fr = fitFoster(*byName("dut7_tank"), 0.0);
        t.check(fr.zModel.poles.size() == 2, "two poles (pair)");
        double w0 = 1.0 / std::sqrt(1e-5 * 1e-10);
        if (fr.zModel.poles.size() == 2) {
            double rel = std::fabs(std::abs(fr.zModel.poles[0]) - w0) / w0;
            t.check(rel < 1e-2, "|p| ~ w0");
        }
        t.end();
    }
    t.begin("parsimony_rejects_overfit");
    {
        FosterResult fr = fitFoster(*byName("dut2a_ser_RL"), 0.005);
        t.check(fr.zModel.poles.empty(), "no finite poles for R+L");
        t.end();
    }
    t.begin("foster1_rc_values");
    {
        FosterResult fr = fitFoster(*byName("dut3a_par_RC"), 0.0);
        const Candidate* f1 = nullptr;
        for (const auto& c : fr.candidates)
            if (!c.skipped && c.note.find("Foster-I") != std::string::npos) f1 = &c;
        t.check(f1 != nullptr, "Foster-I not skipped");
        if (f1) {
            t.check(f1->canonicalStr() == canonical(byName("dut3a_par_RC")->tree),
                    "Foster-I topology = R||C");
            t.check(maxParamError(f1->theta, *byName("dut3a_par_RC")) < 1e-3,
                    "Foster-I param error");
        }
        t.end();
    }
    t.begin("foster2_rl_branch");
    {
        FosterResult fr = fitFoster(*byName("dut2a_ser_RL"), 0.0);
        double best = std::numeric_limits<double>::infinity();
        for (const auto& c : fr.candidates)
            if (!c.skipped) best = std::min(best, c.wrmse);
        t.check(best < 1e-6, "Foster-II reproduces R+L data");
        t.end();
    }
    t.begin("no_negative_elements");
    {
        for (const char* name : {"dut1a_R", "dut3b_par_RL", "dut7_tank"}) {
            FosterResult fr = fitFoster(*byName(name), 0.005);
            for (const auto& c : fr.candidates) {
                if (c.skipped) continue;
                bool finite = true, positive = true;
                for (double v : c.values()) {
                    if (!std::isfinite(v)) finite = false;
                    if (!(v > 0)) positive = false;
                }
                t.check(finite && positive, std::string(name) + " positive elements");
            }
        }
        t.end();
    }

    // ---- extended: model-vs-data for all 12 DUTs, Z and Y domains ----------
    t.begin("rational_fit_wrmse_all_12");
    {
        for (const auto& dut : duts) {
            FosterResult fr = fitFoster(dut, 0.0);
            // wrmse of the rational model itself against the exact data
            Measurement ms = measure(dut, nullptr, 0.0, 0);
            const size_t m = ms.f.size();
            std::vector<Complex> s(m);
            for (size_t k = 0; k < m; ++k) s[k] = Complex(0.0, 1.0) * (2.0 * M_PI * ms.f[k]);
            auto zf = fr.zModel.zFit(s);
            auto [wz, ez] = fitMetrics(ms.z, zf);
            std::vector<Complex> zi(m);
            for (size_t k = 0; k < m; ++k) zi[k] = Complex(1.0, 0.0) / ms.z[k];
            auto yf = fr.yModel.zFit(s);
            auto [wy, ey] = fitMetrics(zi, yf);
            t.check(wz < 1e-6, dut.name + " Z-model wrmse");
            t.check(wy < 1e-6, dut.name + " Y-model wrmse");
        }
        t.end();
    }

    // ---- extended: random real-pole PR systems recovered exactly -----------
    t.begin("random_real_pole_systems_x150");
    {
        auto freqs = defaultFrequencies();
        const size_t m = freqs.size();
        std::vector<Complex> s(m);
        std::vector<double> wAng(m);
        for (size_t k = 0; k < m; ++k) {
            wAng[k] = 2.0 * M_PI * freqs[k];
            s[k] = Complex(0.0, 1.0) * wAng[k];
        }
        double wMin = wAng.front(), wMax = wAng.back();
        auto results = runParallel(150, 8, [&](int i) {
            Rng rng(424242 + (uint64_t)i);
            CaseResult r;
            r.index = i;
            r.name = "randpr[" + std::to_string(i) + "]";
            // random Foster-I-realizable system: d + sum rho_j/(s + a_j)
            int nTerms = 1 + (int)rng.intBelow(3);  // 1..3 real poles
            double d = std::pow(10.0, 1.0 + 2.0 * rng.uniform01());
            std::vector<double> as, rs;
            for (int j = 0; j < nTerms; ++j) {
                double u = rng.uniform01();
                double a = std::exp(std::log(3.0 * wMin) +
                                    u * std::log(wMax / 3.0 / (3.0 * wMin)));
                double rho = std::pow(10.0, 2.0 + 2.0 * rng.uniform01());
                as.push_back(a);
                rs.push_back(rho);
            }
            std::vector<Complex> z(m);
            for (size_t k = 0; k < m; ++k) {
                z[k] = Complex(d, 0.0);
                for (int j = 0; j < nTerms; ++j)
                    z[k] = z[k] + Complex(rs[j], 0.0) / (s[k] + Complex(as[j], 0.0));
            }
            std::vector<double> wts = defaultWeights(z);
            RationalModel model = skRationalFit(wAng, z, wts, 4, 15);
            auto zf = model.zFit(s);
            auto [wrmse, emax] = fitMetrics(z, zf);
            if (!(wrmse < 1e-6)) {
                r.ok = false;
                char buf[96];
                std::snprintf(buf, sizeof(buf), "wrmse %.3g", wrmse);
                r.detail = buf;
            }
            // Foster I must realize an equivalent series circuit
            FosterResult fr = fosterCandidates(wAng, z, wts, 4, 15);
            double best = std::numeric_limits<double>::infinity();
            for (const auto& c : fr.candidates)
                if (!c.skipped) best = std::min(best, c.wrmse);
            if (!(best < 1e-5)) {
                r.ok = false;
                r.detail += "foster wrmse high";
            }
            return r;
        });
        for (auto& r : results) t.addCase(std::move(r));
        t.end();
    }

    // ---- extended: random R||L||C tank sections (c = 0 pairs) --------------
    t.begin("random_tank_sections_x50");
    {
        auto freqs = defaultFrequencies();
        const size_t m = freqs.size();
        std::vector<Complex> s(m);
        std::vector<double> wAng(m);
        for (size_t k = 0; k < m; ++k) {
            wAng[k] = 2.0 * M_PI * freqs[k];
            s[k] = Complex(0.0, 1.0) * wAng[k];
        }
        auto results = runParallel(50, 8, [&](int i) {
            Rng rng(909 + (uint64_t)i);
            CaseResult r;
            r.index = i;
            r.name = "randtank[" + std::to_string(i) + "]";
            // series R0 + R||L||C: resonance inside the band; Q capped at
            // ~50 so the tank damping is identifiable on a 30-point grid
            // (uncapped Q = 1e3+ tanks are dropped as lossless sections by
            // design -- verified identical in the Python reference)
            double f0 = std::exp(std::log(2e3) + rng.uniform01() * std::log(2e6 / 2e3));
            double w0 = 2.0 * M_PI * f0;
            double R0 = std::pow(10.0, 0.5 + 1.5 * rng.uniform01());
            double Rp = std::pow(10.0, 2.0 + 1.5 * rng.uniform01());
            double L = std::pow(10.0, -6.0 + 2.0 * rng.uniform01());
            double C = 1.0 / (w0 * w0 * L);
            double Q = Rp * std::sqrt(C / L);
            if (Q > 50.0) Rp *= 50.0 / Q;
            std::vector<Complex> z(m);
            for (size_t k = 0; k < m; ++k) {
                Complex y = Complex(1.0 / Rp, 0.0) +
                            Complex(1.0, 0.0) / (s[k] * L) + s[k] * C;
                z[k] = Complex(R0, 0.0) + Complex(1.0, 0.0) / y;
            }
            std::vector<double> wts = defaultWeights(z);
            FosterResult fr = fosterCandidates(wAng, z, wts, 4, 15);
            double best = std::numeric_limits<double>::infinity();
            for (const auto& c : fr.candidates)
                if (!c.skipped) best = std::min(best, c.wrmse);
            if (!(best < 1e-5)) {
                r.ok = false;
                char buf[96];
                std::snprintf(buf, sizeof(buf), "foster wrmse %.3g", best);
                r.detail = buf;
            }
            return r;
        });
        for (auto& r : results) t.addCase(std::move(r));
        t.end();
    }

    // ---- extended: F3 bound never exceeds the true reactive count ----------
    t.begin("conservative_bound_x50");
    {
        auto freqs = defaultFrequencies();
        const size_t m = freqs.size();
        std::vector<double> wAng(m);
        std::vector<Complex> s(m);
        for (size_t k = 0; k < m; ++k) {
            wAng[k] = 2.0 * M_PI * freqs[k];
            s[k] = Complex(0.0, 1.0) * wAng[k];
        }
        for (const auto& dut : duts) {
            Measurement ms = measure(dut, nullptr, 0.005, 0);
            std::vector<double> wts = defaultWeights(ms.z);
            RationalModel zm = skRationalFit(wAng, ms.z, wts, 4, 15);
            int trueEnergy = 0;
            for (char k : leafKinds(dut.tree))
                if (k == 'L' || k == 'C') ++trueEnergy;
            t.check(conservativeEnergyBound(zm, s) <= trueEnergy,
                    dut.name + " F3 bound <= truth");
        }
        // random RC ladders: j = 0..order adds order+1 real-pole terms
        for (int i = 0; i < 38; ++i) {
            Rng rng(31337 + i);
            int order = 1 + (int)rng.intBelow(3);
            std::vector<Complex> z(m, Complex(0.0, 0.0));
            for (int j = 0; j <= order; ++j) {
                double R = std::pow(10.0, 1.0 + 3.0 * rng.uniform01());
                double C = std::exp(std::log(1e-11) + rng.uniform01() * std::log(1e-5 / 1e-11));
                double a = 1.0 / (R * C);
                Complex rho = 1.0 / C;
                for (size_t k = 0; k < m; ++k)
                    z[k] = z[k] + rho / (s[k] + Complex(a, 0.0));
            }
            std::vector<double> wts = defaultWeights(z);
            RationalModel zm = skRationalFit(wAng, z, wts, 4, 15);
            t.check(conservativeEnergyBound(zm, s) <= order + 1,
                    "random RC ladder " + std::to_string(i) + " bound <= terms");
        }
        t.end();
    }
}

// ===========================================================================
// pruning: F2 / F3 never prune the truth (and do prune something)
// ===========================================================================

void suitePruning(TestCtx& t) {
    t.suite = "pruning";
    auto duts = makeDuts();

    t.begin("f2_never_prunes_truth_all_12");
    {
        for (const auto& dut : duts) {
            int n = std::max(4, nLeaves(dut.tree));
            auto& lib = TopologyLibrary::get(n);
            Measurement ms = measure(dut, nullptr, 0.005, 0);
            const size_t m = ms.f.size();
            std::vector<double> wAng(m);
            for (size_t k = 0; k < m; ++k) wAng[k] = 2.0 * M_PI * ms.f[k];
            AsymptoticFeatures feat = extractAsymptotics(wAng, ms.z);
            bool kept = false;
            for (const auto& tr : lib) {
                if (pruneF2(tr, feat) && canonical(tr) == canonical(dut.tree)) kept = true;
            }
            t.check(kept, dut.name + " F2 keeps truth");
        }
        t.end();
    }
    t.begin("f3_never_prunes_truth");
    {
        for (const auto& dut : duts) {
            int trueEnergy = 0;
            for (char k : leafKinds(dut.tree))
                if (k == 'L' || k == 'C') ++trueEnergy;
            t.check(pruneF3(dut.tree, trueEnergy), dut.name + " F3 keeps truth");
        }
        t.end();
    }
    t.begin("f2_actually_prunes");
    {
        const DUT* dut = nullptr;
        for (const auto& d : duts)
            if (d.name == "dut1c_C") dut = &d;
        Measurement ms = measure(*dut, nullptr, 0.005, 0);
        const size_t m = ms.f.size();
        std::vector<double> wAng(m);
        for (size_t k = 0; k < m; ++k) wAng[k] = 2.0 * M_PI * ms.f[k];
        AsymptoticFeatures feat = extractAsymptotics(wAng, ms.z);
        auto& lib = TopologyLibrary::get(4);
        int kept = 0;
        for (const auto& tr : lib)
            if (pruneF2(tr, feat)) ++kept;
        t.check(kept < (int)lib.size(), "F2 prunes on pure C");
        t.end();
    }
    t.begin("prune_fallback_never_empty");
    {
        const DUT* dut = nullptr;
        for (const auto& d : duts)
            if (d.name == "dut1a_R") dut = &d;
        Measurement ms = measure(*dut, nullptr, 0.005, 0);
        const size_t m = ms.f.size();
        std::vector<double> wAng(m);
        for (size_t k = 0; k < m; ++k) wAng[k] = 2.0 * M_PI * ms.f[k];
        AsymptoticFeatures feat = extractAsymptotics(wAng, ms.z);
        auto& lib = TopologyLibrary::get(2);
        auto out = pruneTrees(lib, feat, 99, true, true);
        t.check(out.size() == lib.size(), "fallback returns full library");
        t.end();
    }
    t.begin("slope_ranges");
    {
        t.check(highFreqSlopeRange(T_leaf('L')) == std::make_pair(1, 1), "L slope +1");
        t.check(highFreqSlopeRange(T_leaf('C')) == std::make_pair(-1, -1), "C slope -1");
        t.check(highFreqSlopeRange(T_node(NK::Ser, {T_leaf('R'), T_leaf('L')})) ==
                    std::make_pair(1, 1),
                "R+L slope +1");
        auto rc = T_node(NK::Par, {T_leaf('R'), T_leaf('C')});
        t.check(highFreqSlopeRange(rc) == std::make_pair(-1, -1), "R||C slope -1");
        auto rl = T_node(NK::Ser, {T_leaf('R'), T_node(NK::Par, {T_leaf('R'), T_leaf('L')})});
        // at s -> inf the R||L branch is dominated by R (L opens), so the
        // whole network flattens: slope exactly 0
        t.check(highFreqSlopeRange(rl) == std::make_pair(0, 0), "R+(R||L) slope 0");
        t.end();
    }
}

// ===========================================================================
// selector: equivalence clustering
// ===========================================================================

void suiteSelector(TestCtx& t) {
    t.suite = "selector";
    auto freqs = defaultFrequencies();

    t.begin("grid_shape");
    {
        auto g = makeValidationGrid(freqs);
        t.check(g.size() == 200, "200 points");
        t.checkClose(g.front(), freqs.front() / 10.0, 1e-12, "low edge /10");
        t.checkClose(g.back(), freqs.back() * 10.0, 1e-12, "high edge x10");
        t.end();
    }
    t.begin("trivial_equivalence_R_plus_R");
    {
        // two series-R candidates vs a single R of the summed value: identical
        // responses must merge into one class
        Assembled ser2 = assemble(NK::Ser, {Assembled{T_leaf('R'), {400.0}},
                                            Assembled{T_leaf('R'), {600.0}}});
        Candidate c1;
        c1.tree = ser2.tree;
        c1.theta = {std::log10(400.0), std::log10(600.0)};
        c1.rss = 0.0;
        c1.aiccVal = 0.0;
        Candidate c2;
        c2.tree = T_leaf('R');
        c2.theta = {3.0};  // 1 kOhm
        c2.rss = 0.0;
        c2.aiccVal = 0.0;
        auto grid = makeValidationGrid(freqs);
        t.check(areEquivalent(c1, c2, grid, 1e-9), "R+R equivalent to R");
        t.end();
    }
    t.begin("nonequivalence_detected");
    {
        Candidate c1;
        c1.tree = T_leaf('R');
        c1.theta = {3.0};
        Candidate c2;
        c2.tree = T_leaf('C');
        c2.theta = {-8.0};
        auto grid = makeValidationGrid(freqs);
        t.check(!areEquivalent(c1, c2, grid, 1e-3), "R not equivalent to C");
        t.end();
    }
    t.begin("cluster_merges_equivalents");
    {
        // best = 2-param model at noise floor; equivalent 3-param superset
        Candidate a;
        a.tree = T_node(NK::Par, {T_leaf('R'), T_leaf('C')});
        a.theta = {3.0, -8.0};
        a.rss = 1e-6;
        a.aiccVal = aicc(a.rss, 60, 2);
        Candidate b;
        b.tree = T_node(NK::Par, {T_leaf('R'), T_leaf('C')});
        b.theta = {3.0 + 1e-9, -8.0 + 1e-9};
        b.rss = 1.1e-6;
        b.aiccVal = aicc(b.rss, 60, 2);
        std::vector<Candidate> pool{a, b};
        auto classes = rankAndClusterEquivalent(pool, freqs, 1e-3, 60);
        t.check(classes.size() == 1, "merged into one class");
        t.check(classes[0].members.size() == 2, "two members");
        t.end();
    }
    t.begin("secondary_sort_prefers_fewer_elements");
    {
        Candidate many;
        many.tree = T_node(NK::Ser, {T_leaf('R'), T_leaf('L'), T_leaf('C')});
        many.theta = {3.0, -3.0, -8.0};
        Candidate few;
        few.tree = T_leaf('R');
        few.theta = {3.0};
        t.check(secondarySortKey(few) < secondarySortKey(many), "fewer params first");
        t.end();
    }
    t.begin("foster_duality_tank_equivalence");
    {
        // Foster-I and Foster-II of the same tank must be electrically equal
        Assembled tank = assemble(
            NK::Par, {Assembled{T_leaf('R'), {1e3}}, Assembled{T_leaf('L'), {1e-5}},
                      Assembled{T_leaf('C'), {1e-10}}});
        Assembled serTank = assemble(
            NK::Ser,
            {assemble(NK::Par, {Assembled{T_leaf('L'), {1e-5}},
                                Assembled{T_leaf('C'), {1e-10}}}),
             Assembled{T_leaf('R'), {1e3}}});
        Measurement ms;
        ms.f = freqs;
        ms.z.resize(freqs.size());
        auto th = tank.values;
        std::vector<double> theta(th.size());
        for (size_t i = 0; i < th.size(); ++i) theta[i] = std::log10(th[i]);
        evalThetaFreq(tank.tree, theta, freqs.data(), freqs.size(), ms.z.data());
        std::vector<double> wAng(freqs.size());
        for (size_t k = 0; k < freqs.size(); ++k) wAng[k] = 2.0 * M_PI * freqs[k];
        FosterResult fr = fosterCandidates(wAng, ms.z, defaultWeights(ms.z), 4, 15);
        int nonSkipped = 0;
        double bestW = std::numeric_limits<double>::infinity();
        for (const auto& c : fr.candidates) {
            if (c.skipped) continue;
            ++nonSkipped;
            bestW = std::min(bestW, c.wrmse);
        }
        t.check(nonSkipped >= 1, "at least one Foster candidate");
        t.check(bestW < 1e-6, "Foster realizes tank to machine precision");
        // structure check: Foster-I canonical equals the tank canonical
        bool sawTank = false;
        for (const auto& c : fr.candidates) {
            if (!c.skipped && c.canonicalStr() == canonical(tank.tree)) sawTank = true;
        }
        t.check(sawTank, "Foster-I topology == R||L||C");
        (void)serTank;
        t.end();
    }
}

// ===========================================================================
// end-to-end: 12 DUTs, noiseless + noisy (mirrors tests/test_end_to_end.py)
// ===========================================================================

static std::string classifyDut(const DUT& dut, const IdentifyResult& res,
                               const std::vector<double>& f, double equivTol,
                               double& paramErr) {
    paramErr = -1.0;
    if (res.classes.empty()) return "MISS";
    const EquivalenceClass& best = res.classes[0];
    const Candidate& rep = best.representative;
    if (canonical(rep.tree) == canonical(dut.tree)) {
        paramErr = maxParamError(rep.theta, dut);
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

void suiteEndToEnd(TestCtx& t) {
    t.suite = "end_to_end";
    auto duts = makeDuts();
    for (const auto& dut : duts) {
        t.begin("noiseless/" + dut.name);
        {
            Measurement ms = measure(dut, nullptr, 0.0, 0);
            Config cfg;
            if (nLeaves(dut.tree) > 4) cfg.maxN = 5;
            IdentifyResult res = identify(ms.f, ms.z, nullptr, &cfg);
            double perr;
            std::string status = classifyDut(dut, res, ms.f, 1e-6, perr);
            t.check(status != "MISS", "noiseless top-1 matches (" + status + ")");
            if (status == "EXACT") t.check(perr < 1e-4, "param error < 1e-4");
            t.end();
        }
        t.begin("noisy/" + dut.name);
        {
            Measurement ms = measure(dut, nullptr, 0.005, 0);
            Config cfg;
            if (nLeaves(dut.tree) > 4) cfg.maxN = 5;
            IdentifyResult res = identify(ms.f, ms.z, nullptr, &cfg);
            double perr;
            std::string status = classifyDut(dut, res, ms.f, 2e-2, perr);
            t.check(status != "MISS", "noisy top-1 matches (" + status + ")");
            if (status == "EXACT") t.check(perr < 0.02, "param error < 2%");
            t.end();
        }
    }
}

}  // namespace rlctest
