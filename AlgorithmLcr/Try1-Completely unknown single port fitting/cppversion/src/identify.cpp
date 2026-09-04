#include "identify.hpp"

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

    // 2. prune library (F2 asymptotics + F3 conservative energy bound);
    //    with the exactN prior the library is the single N-device layer
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

    // 4. selector: merge engines, cluster equivalents, noise-consistent
    //    parsimony ranking (D6 + discrepancy principle); the exactN prior
    //    drops engine-B candidates whose device count differs
    std::vector<Candidate> pool = std::move(fitsA);
    for (const auto& c : foster.candidates) {
        if (c.skipped) continue;
        if (cfg.exactN.has_value() && nLeaves(c.tree) != *cfg.exactN) continue;
        pool.push_back(c);
    }
    std::vector<EquivalenceClass> classes =
        rankAndClusterEquivalent(std::move(pool), f, kEquivMaxRelTol, (int)(2 * m));

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
