#include "identify.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace rlc {

IdentifyResult identify(const std::vector<double>& f, const std::vector<Complex>& z,
                        const std::vector<double>* weights, const Config* configIn) {
    Config cfg = configIn ? *configIn : Config();
    if (cfg.exactN.has_value() && *cfg.exactN < 1)
        throw std::invalid_argument("exactN must be a positive device count");
    const size_t m = f.size();
    std::vector<double> w(m);
    for (size_t k = 0; k < m; ++k) w[k] = 2.0 * M_PI * f[k];
    std::vector<Complex> s(m);
    for (size_t k = 0; k < m; ++k) s[k] = Complex(0.0, 1.0) * w[k];
    std::vector<double> wts = weights ? *weights : defaultWeights(z);

    // 0. asymptotic features (F2 pre-judgement, heuristic starts)
    AsymptoticFeatures features = extractAsymptotics(w, z);
    StartHints hints = hintsFromFeatures(features);

    // 1. engine B: rational fit + Foster synthesis (closed-form candidates)
    FosterResult foster =
        fosterCandidates(w, z, wts, cfg.maxOrder, cfg.skIters);

    // 2. prune library (F2 asymptotics — destructive) and order it by
    //    ascending energy storage (F3 demoted to a scheduling key in R1 — see
    //    OPTIMIZATION_LOG.md; with the exactN prior the library is the single
    //    N-device layer)
    std::vector<TreePtr> libStore;
    const std::vector<TreePtr>* lib;
    if (cfg.exactN.has_value()) {
        libStore = TopologyLibrary::ofSize(*cfg.exactN, cfg.maxIDepth);
        lib = &libStore;
    } else {
        lib = &TopologyLibrary::get(cfg.maxN, cfg.maxIDepth);
    }
    int minEnergy = conservativeEnergyBound(foster.zModel, s);
    std::vector<TreePtr> kept =
        pruneTrees(*lib, features, minEnergy, cfg.enableF2, cfg.enableF3);

    // 3. engine A: two-stage multi-start fit; Foster solutions as starts
    std::map<std::string, std::vector<std::vector<double>>> extraStarts;
    for (const auto& cand : foster.candidates) {
        if (!cand.skipped) extraStarts[cand.canonicalStr()].push_back(cand.theta);
    }
    std::vector<Candidate> fitsA = fitLibrary(kept, s, z, wts, cfg.engineAConfig(),
                                              &hints, &extraStarts);

    // R7: outlier-robust refit of the leading engine-A candidates.  Real
    // sweeps contain a few wild points; a plain least-squares fit bends
    // towards them, which both inflates the wRMSE of the true structure and
    // biases its parameters.  The robust pass re-fits from the candidate's
    // own solution with outlier points downweighted (see fit_engine_a.cpp).
    {
        const size_t kRobustTop = 12;
        for (size_t i = 0; i < fitsA.size() && i < kRobustTop; ++i)
            robustRefitCandidate(fitsA[i], s, z, wts);
        // the funnel ordered by aicc — re-sort after metric updates
        std::stable_sort(fitsA.begin(), fitsA.end(),
                         [](const Candidate& a, const Candidate& b) {
                             return a.aiccVal < b.aiccVal;
                         });
    }

    // 4. selector: merge engines, cluster equivalents, noise-consistent
    //    parsimony ranking (D6 + discrepancy principle); the exactN prior
    //    drops engine-B candidates whose device count differs
    std::vector<Candidate> pool = std::move(fitsA);
    for (const auto& c : foster.candidates) {
        if (c.skipped) continue;
        if (cfg.exactN.has_value() && nLeaves(c.tree) != *cfg.exactN) continue;
        pool.push_back(c);
    }
    std::vector<EquivalenceClass> classes = rankAndClusterEquivalent(
        std::move(pool), f, kEquivMaxRelTol, (int)(2 * m),
        estimateRelativeNoise(w, z));

    IdentifyResult out;
    out.classes = std::move(classes);
    out.features = features;
    out.zModel = std::move(foster.zModel);
    out.yModel = std::move(foster.yModel);
    out.foster = std::move(foster.candidates);
    out.nLibrary = (int)lib->size();
    out.nPrunedKept = (int)kept.size();
    return out;
}

}  // namespace rlc
