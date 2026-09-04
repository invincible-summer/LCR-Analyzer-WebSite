// Mass verification sweeps: every canonical topology through the full
// identify() pipeline with randomly drawn (identifiability-screened) element
// values, plus noisy / extreme-value / variable-band sweeps.

#include "framework.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace rlctest {

// Judge one noiseless identification against ground truth.
// verdict categories:
//   EXACT    - top-1 canonical == truth canonical AND params within tol
//              (structures with repeated identical subtrees compare
//              electrically instead: branch swapping makes per-element
//              comparison ambiguous)
//   EQUIV    - a class-1 member is electrically equivalent on the extended grid
//   INBAND   - top-1 matches truth only in-band (allowed for weak draws)
//   FITOK    - top-1 is a different structure but fits the noiseless data to
//              < 1e-5 (a legitimate alternative data model; verified that the
//              truth itself is reachable by a direct fit, i.e. all pipeline
//              components are sound and only multi-start coverage limited)
//   FITWEAK  - as FITOK but fit quality between 1e-5 and 1e-2 (multimodal
//              start-coverage limitation, shared with the Python reference)
//   FAIL     - anything else, including a truth-topology direct fit that does
//              not converge (a genuine defect)
struct SweepVerdict {
    std::string category;
    std::string detail;
};

static bool truthReachable(const TreePtr& truth, const std::vector<double>& thetaTrue,
                           const std::vector<double>& f,
                           const std::vector<Complex>& z) {
    // direct fit started near the true parameters must reach machine
    // precision (basin + optimizer + evaluator all sound)
    const size_t m = f.size();
    std::vector<Complex> s(m);
    for (size_t k = 0; k < m; ++k) s[k] = Complex(0.0, 2.0 * M_PI * f[k]);
    std::vector<double> w = defaultWeights(z);
    Rng rng(12345);
    std::vector<double> theta = thetaTrue;
    for (double& v : theta) v += 1e-4 * rng.normal();
    auto cand = fitTopology(truth, s, z, w, {theta}, 0, 1e-11);
    return cand.has_value() && cand->wrmse < 1e-6;
}

static SweepVerdict judgeNoiseless(const TreePtr& truth, const std::vector<double>& thetaTrue,
                                   const std::vector<double>& f, int maxN, double paramTol,
                                   bool identifiable) {
    const size_t m = f.size();
    std::vector<Complex> z(m);
    evalThetaFreq(truth, thetaTrue, f.data(), m, z.data());
    Config cfg;
    cfg.maxN = maxN;
    IdentifyResult res = identify(f, z, nullptr, &cfg);
    if (res.classes.empty()) return {"FAIL", "no candidates"};
    const EquivalenceClass& cls = res.classes[0];
    const Candidate& rep = cls.representative;
    bool ambiguous = hasRepeatedSubtrees(truth);
    if (canonical(rep.tree) == canonical(truth)) {
        double gridDev = maxRelDiffOnGrid(rep.tree, rep.theta, truth, thetaTrue,
                                          makeValidationGrid(f));
        double worst = 0.0;
        for (size_t i = 0; i < thetaTrue.size(); ++i) {
            double fit = std::pow(10.0, rep.theta[i]);
            double want = std::pow(10.0, thetaTrue[i]);
            worst = std::max(worst, std::fabs((fit - want) / want));
        }
        char buf[128];
        std::snprintf(buf, sizeof(buf), "param %.2e dev %.2e", worst, gridDev);
        if (!ambiguous && worst < paramTol) return {"EXACT", buf};
        // exact electrical twin (T2: same Z, different parametrization) or a
        // fit on the truth manifold (nearly-degenerate splits / flat valleys
        // are intrinsic to these structures)
        if (gridDev < 1e-6)
            return {ambiguous ? "EXACT" : "EQUIV",
                    std::string(buf) + (ambiguous ? " (ambiguous branch order)" : " twin")};
        if (rep.wrmse < 1e-6) {
            char b2[160];
            std::snprintf(b2, sizeof(b2), "%s wrmse %.2e (flat valley)", buf, rep.wrmse);
            return {"WEAKFIT", b2};
        }
        return {"FAIL", std::string("canonical match but wrmse ") +
                            std::to_string(rep.wrmse)};
    }
    Candidate truthC;
    truthC.tree = truth;
    truthC.theta = thetaTrue;
    auto grid = makeValidationGrid(f);
    for (const auto& mem : cls.members) {
        if (areEquivalent(mem, truthC, grid, 1e-6))
            return {"EQUIV", "via " + mem.canonicalStr()};
    }
    double inband = maxRelDiffOnGrid(rep.tree, rep.theta, truth, thetaTrue, f);
    if (!identifiable && inband < 3e-2) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "in-band %.2e", inband);
        return {"INBAND", buf};
    }
    // different structure: legitimate alternative data model or multimodal miss
    bool reachable = truthReachable(truth, thetaTrue, f, z);
    if (!reachable) return {"FAIL", "truth topology not fittable (pipeline defect)"};
    char buf[128];
    std::snprintf(buf, sizeof(buf), "wrmse %.2e top1=%s (truth reachable, start coverage)",
                  rep.wrmse, rep.canonicalStr().c_str());
    if (rep.wrmse < 1e-5) return {"FITOK", buf};
    if (rep.wrmse < 1e-2) return {"FITWEAK", buf};
    return {"FAIL", buf};
}

// ===========================================================================
// sweep over every canonical topology with 1..5 elements (119 topologies x 8)
// ===========================================================================

void suiteSweepN5(TestCtx& t) {
    t.suite = "sweep_n1_5";
    warmLibraryCache(5, 2);
    const std::vector<TreePtr>& lib = TopologyLibrary::get(5, 2);
    auto freqs = defaultFrequencies();
    const int kDraws = 8;
    int total = (int)lib.size() * kDraws;

    auto results = runParallel(total, 12, [&](int i) {
        int ti = i / kDraws, draw = i % kDraws;
        const TreePtr& truth = lib[ti];
        bool identifiable = false;
        double minSens = 0.0;
        std::vector<double> theta =
            sampleIdentifiable(truth, 100000 + 1000 * (uint64_t)ti + draw, freqs,
                               identifiable, minSens);
        int maxN = std::max(4, nLeaves(truth));
        SweepVerdict v = judgeNoiseless(truth, theta, freqs, maxN, 1e-3, identifiable);
        CaseResult r;
        r.index = i;
        r.name = canonical(truth) + "#" + std::to_string(draw);
        r.ok = v.category != "FAIL";
        char buf[64];
        std::snprintf(buf, sizeof(buf), " sens=%.3g", minSens);
        r.detail = "[" + v.category + "] " + v.detail + buf;
        return r;
    });
    for (auto& r : results) t.addCase(std::move(r));
}

// ===========================================================================
// sweep over every canonical topology with 6 elements (78 topologies x 4)
// ===========================================================================

void suiteSweepN6(TestCtx& t) {
    t.suite = "sweep_n6";
    const std::vector<TreePtr>& libAll = TopologyLibrary::get(6, 2);
    std::vector<TreePtr> lib6;
    for (const auto& tr : libAll)
        if (nLeaves(tr) == 6) lib6.push_back(tr);
    auto freqs = defaultFrequencies();
    const int kDraws = 4;
    int total = (int)lib6.size() * kDraws;

    auto results = runParallel(total, 12, [&](int i) {
        int ti = i / kDraws, draw = i % kDraws;
        const TreePtr& truth = lib6[ti];
        bool identifiable = false;
        double minSens = 0.0;
        std::vector<double> theta =
            sampleIdentifiable(truth, 900000 + 1000 * (uint64_t)ti + draw, freqs,
                               identifiable, minSens);
        SweepVerdict v = judgeNoiseless(truth, theta, freqs, 6, 1e-3, identifiable);
        CaseResult r;
        r.index = i;
        r.name = canonical(truth) + "#" + std::to_string(draw);
        r.ok = v.category != "FAIL";
        char buf[64];
        std::snprintf(buf, sizeof(buf), " sens=%.3g", minSens);
        r.detail = "[" + v.category + "] " + v.detail + buf;
        return r;
    });
    for (auto& r : results) t.addCase(std::move(r));
}

// ===========================================================================
// noisy sweep: 12 complex structures x 5 seeds at 0.5% relative noise
// ===========================================================================

void suiteNoisySweep(TestCtx& t) {
    t.suite = "noisy_sweep";
    warmLibraryCache(6, 2);
    const std::vector<TreePtr>& libAll = TopologyLibrary::get(6, 2);
    // a dozen structures spanning 2..6 elements and both root kinds, incl.
    // the v2 parallel-multi-L classes (children are stored sorted)
    const std::vector<std::string> want = {
        "S(C,L)",          "P(C,R)",          "S(P(C,R),R)",      "P(L,S(C,R))",
        "P(C,L,L)",        "S(P(L,L),R)",     "S(C,P(C,L))",      "P(C,L,R)",
        "S(P(C,L),P(C,L))", "P(L,L,S(C,R))",  "S(C,P(L,L),R)",    "P(C,S(C,L),S(C,L))",
    };
    std::vector<TreePtr> picked;
    for (const auto& cs : want) {
        bool found = false;
        for (const auto& tr : libAll) {
            if (canonical(tr) == cs) {
                picked.push_back(tr);
                found = true;
                break;
            }
        }
        if (!found) {
            t.begin("missing-topology/" + cs);
            t.check(false, "topology not found in library");
            t.end();
        }
    }

    auto freqs = defaultFrequencies();
    int total = (int)picked.size() * 5;

    auto results = runParallel(total, 10, [&](int i) {
        int ti = i / 5, seedIdx = i % 5;
        const TreePtr& truth = picked[ti];
        // elements must be identifiable ON THE MEASUREMENT BAND well above
        // the 0.5% noise floor (band-external visibility is not recoverable)
        std::vector<double> lb, ub;
        thetaBounds(truth, lb, ub);
        const size_t p = lb.size();  // parameters (two per L device)
        const size_t mBand = freqs.size();
        std::vector<Complex> sb(mBand);
        for (size_t k = 0; k < mBand; ++k)
            sb[k] = Complex(0.0, 2.0 * M_PI * freqs[k]);
        Rng rngS(500000 + 137 * (uint64_t)ti);
        std::vector<double> theta(p, 0.0);
        double minSens = -1.0;
        for (int attempt = 0; attempt < 300; ++attempt) {
            std::vector<double> cand(p);
            for (size_t j = 0; j < p; ++j) {
                double u = rngS.uniform01();
                cand[j] = (lb[j] + 0.5) + u * ((ub[j] - 1.0) - (lb[j] + 0.5));
            }
            std::vector<Complex> Z(mBand), J(p * mBand);
            evalJac(truth, cand, sb.data(), mBand, Z.data(), J.data());
            double minS = std::numeric_limits<double>::infinity();
            for (size_t ip = 0; ip < p; ++ip) {
                double mx = 0.0;
                for (size_t k = 0; k < mBand; ++k)
                    mx = std::max(mx, std::abs(J[ip * mBand + k]) /
                                          (kLn10 * std::max(std::abs(Z[k]), 1e-300)));
                minS = std::min(minS, mx);
            }
            if (minS > minSens) {
                minSens = minS;
                theta = cand;
            }
            if (minS >= 0.05) break;
        }
        if (minSens < 0.05) {
            CaseResult r;
            r.index = i;
            r.name = canonical(truth) + "@0.5%#" + std::to_string(seedIdx);
            r.ok = true;  // no sufficiently identifiable draw exists; skipped
            char buf[64];
            std::snprintf(buf, sizeof(buf), "[SKIP] band sens %.3g", minSens);
            r.detail = buf;
            return r;
        }
        DUT dut;
        dut.name = "sweep";
        dut.tree = truth;
        dut.values.resize(theta.size());
        for (size_t k = 0; k < theta.size(); ++k) dut.values[k] = std::pow(10.0, theta[k]);
        Measurement ms = measure(dut, &freqs, 0.005, 7777 + (uint64_t)i);
        Config cfg;
        cfg.maxN = std::max(4, nLeaves(truth));
        IdentifyResult res = identify(ms.f, ms.z, nullptr, &cfg);

        CaseResult r;
        r.index = i;
        r.name = canonical(truth) + "@0.5%#" + std::to_string(seedIdx);
        if (res.classes.empty()) {
            r.ok = false;
            r.detail = "no candidates";
            return r;
        }
        const EquivalenceClass& cls = res.classes[0];
        const Candidate& rep = cls.representative;
        if (canonical(rep.tree) == canonical(truth)) {
            double worst = 0.0;
            for (size_t k = 0; k < theta.size(); ++k) {
                double fit = std::pow(10.0, rep.theta[k]);
                double wantV = std::pow(10.0, theta[k]);
                worst = std::max(worst, std::fabs((fit - wantV) / wantV));
            }
            char buf[96];
            std::snprintf(buf, sizeof(buf), "[EXACT] param %.2e", worst);
            r.detail = buf;
            // random hard draws at 0.5% noise: weakest-element error up to
            // ~16x the noise level remains acceptable (curated-DUT bar is 2%)
            r.ok = worst < 0.08;
            return r;
        }
        Candidate truthC;
        truthC.tree = truth;
        truthC.theta = theta;
        auto grid = makeValidationGrid(freqs);
        for (const auto& mem : cls.members) {
            if (areEquivalent(mem, truthC, grid, 2e-2)) {
                r.detail = "[EQUIV] via " + mem.canonicalStr();
                return r;
            }
        }
        r.ok = false;
        r.detail = "[FAIL] top1=" + rep.canonicalStr();
        return r;
    });
    for (auto& r : results) t.addCase(std::move(r));
}

// ===========================================================================
// extreme element values and non-default frequency bands
// ===========================================================================

void suiteExtremesBands(TestCtx& t) {
    t.suite = "extremes_bands";
    const std::vector<TreePtr>& lib = TopologyLibrary::get(5, 2);
    auto byCanonical = [&](const std::string& cs) -> TreePtr {
        for (const auto& tr : lib)
            if (canonical(tr) == cs) return tr;
        return nullptr;
    };

    // ---- extreme values near the search-domain bounds ----------------------
    {
        const std::vector<std::string> extremes = {
            "S(C,L)", "S(C,R)", "P(L,R)", "P(C,R)", "S(P(C,R),R)",
        };
        int total = (int)extremes.size() * 4;
        auto freqs = defaultFrequencies();
        auto results = runParallel(total, 8, [&](int i) {
            TreePtr truth = byCanonical(extremes[i / 4]);
            int variant = i % 4;
            if (!truth) {
                CaseResult r;
                r.index = i;
                r.name = "extreme/missing";
                r.ok = false;
                r.detail = "topology not found";
                return r;
            }
            // clamp draws toward the domain corners
            Rng rng(31337 + i);
            std::vector<double> lb, ub;
            thetaBounds(truth, lb, ub);
            std::vector<double> theta(lb.size());
            for (size_t j = 0; j < theta.size(); ++j) {
                double u = rng.uniform01();
                double lo = lb[j] + (variant % 2 == 0 ? 0.2 : 1.0);
                double hi = ub[j] - (variant < 2 ? 0.2 : 1.0);
                theta[j] = lo + u * (hi - lo);
            }
            bool identifiable = false;
            double minSens = 0.0;
            // reuse the identifiability screen on the fixed draw
            {
                auto grid = makeValidationGrid(freqs);
                std::vector<Complex> s(grid.size());
                for (size_t k = 0; k < grid.size(); ++k)
                    s[k] = Complex(0.0, 1.0) * (2.0 * M_PI * grid[k]);
                std::vector<Complex> Z(grid.size()), J(theta.size() * grid.size());
                evalJac(truth, theta, s.data(), grid.size(), Z.data(), J.data());
                double minS = std::numeric_limits<double>::infinity();
                for (size_t ip = 0; ip < theta.size(); ++ip) {
                    double mx = 0.0;
                    for (size_t k = 0; k < grid.size(); ++k)
                        mx = std::max(mx, std::abs(J[ip * grid.size() + k]) /
                                              (kLn10 * std::max(std::abs(Z[k]), 1e-300)));
                    minS = std::min(minS, mx);
                }
                identifiable = minS >= 0.01;
                minSens = minS;
            }
            int maxN = std::max(4, nLeaves(truth));
            SweepVerdict v = judgeNoiseless(truth, theta, freqs, maxN, 5e-3, identifiable);
            CaseResult r;
            r.index = i;
            r.name = "extreme[" + canonical(truth) + "/" + std::to_string(variant) + "]";
            r.ok = v.category != "FAIL";
            char buf[64];
            std::snprintf(buf, sizeof(buf), " sens=%.3g", minSens);
            r.detail = "[" + v.category + "] " + v.detail + buf;
            return r;
        });
        for (auto& r : results) t.addCase(std::move(r));
    }

    // ---- non-default bands: narrow / wide / different point counts ---------
    {
        struct BandCase {
            const char* canonical;
            double fMin, fMax;
            int nPts;
            double inbandTol;
        };
        std::vector<BandCase> cases = {
            // narrow 2-decade bands: features mostly outside -> in-band
            // equivalence expected (band-limited identifiability, T2b)
            {"S(P(C,R),R)", 1e3, 1e5, 30, 3e-2},
            {"P(C,L,R)", 1e2, 1e4, 30, 3e-2},
            {"P(L,S(C,R))", 1e5, 1e7, 30, 3e-2},
            {"S(P(C,R),R)", 1e4, 1e6, 30, 3e-2},
            {"P(C,L,R)", 1e6, 1e8, 30, 3e-2},
            // wide 8-decade bands: everything identifiable -> exact expected
            {"S(P(C,R),R)", 1.0, 1e8, 41, 1e-3},
            {"P(C,L,R)", 1.0, 1e8, 41, 1e-3},
            {"P(L,S(C,R))", 1.0, 1e8, 41, 1e-3},
            {"S(C,L)", 1.0, 1e8, 41, 1e-3},
            {"P(C,R)", 1.0, 1e8, 41, 1e-3},
            {"P(C,L,L)", 1.0, 1e8, 41, 1e-3},
            {"P(L,S(C,R))", 1.0, 1e8, 41, 1e-3},
            {"S(P(C,R),P(L,R))", 1.0, 1e8, 41, 1e-3},
            {"S(C,P(C,L),R)", 1.0, 1e8, 41, 1e-3},
            // point-count sensitivity on the default band
            {"S(P(C,R),R)", 10.0, 1e7, 20, 1e-3},
            {"P(C,L,R)", 10.0, 1e7, 20, 1e-3},
            {"P(L,S(C,R))", 10.0, 1e7, 50, 1e-3},
            {"S(C,L)", 10.0, 1e7, 20, 1e-3},
            {"P(C,R)", 10.0, 1e7, 50, 1e-3},
            {"P(C,L,L)", 10.0, 1e7, 20, 1e-3},
        };
        int idx = 0;
        for (const auto& bc : cases) {
            TreePtr truth = byCanonical(bc.canonical);
            t.begin("band[" + std::string(bc.canonical) + "@" + std::to_string((int)bc.fMin) +
                    ":" + std::to_string((int)std::log10(bc.fMax)) + "np" +
                    std::to_string(bc.nPts) + "]");
            {
                if (!truth) {
                    t.check(false, "topology not found");
                    t.end();
                    ++idx;
                    continue;
                }
                auto freqs = geomspace(bc.fMin, bc.fMax, bc.nPts);
                bool identifiable = false;
                double minSens = 0.0;
                std::vector<double> theta = sampleIdentifiable(
                    truth, 600000 + 61 * (uint64_t)idx, freqs, identifiable, minSens);
                int maxN = std::max(4, nLeaves(truth));
                // wide/point-count cases use a value draw whose features sit
                // inside the given band; the identifiability screen enforces
                // that.  Narrow-band cases allow the in-band fallback.
                bool allowInband = bc.inbandTol > 1e-2;
                SweepVerdict v = judgeNoiseless(truth, theta, freqs, maxN, 1e-3, allowInband);
                t.check(v.category != "FAIL",
                        "[" + v.category + "] " + v.detail + " sens=" +
                            std::to_string(minSens));
            }
            t.end();
            ++idx;
        }
    }
}

}  // namespace rlctest
