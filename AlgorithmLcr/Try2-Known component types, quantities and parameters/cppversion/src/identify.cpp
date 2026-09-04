#include "identify.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ng {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

IdentifyResult identify(const ComponentSet& compset, const std::vector<double>& f,
                        const std::vector<Complex>& z,
                        const std::vector<double>* weights, const Config* config) {
    Config cfg = config ? *config : Config();
    if (f.size() != z.size()) throw std::invalid_argument("f and z must have the same length");
    std::vector<double> w = weights ? *weights : defaultWeights(z);
    std::vector<Complex> s(f.size());
    for (size_t k = 0; k < f.size(); ++k) s[k] = Complex(0.0, 1.0) * (2.0 * kPi * f[k]);

    auto t0 = std::chrono::steady_clock::now();
    FunnelState state =
        runFunnel(compset, s, z, w, cfg.coarsePoints, cfg.funnelRatio, cfg.funnelMinKeep,
                  cfg.batchSize, cfg.allowDead);
    auto t1 = std::chrono::steady_clock::now();

    std::vector<Network> survivors = state.finalKeep();
    std::vector<Candidate> candidates =
        evaluateCandidates(survivors, compset, s, z, w, cfg.batchSize);
    if (cfg.refineValues) {
        // two passes (R5): the first pass re-ranks structures by refined RSS,
        // so the second pass polishes the promoted candidates from their
        // first-pass values
        candidates = refineTopCandidates(std::move(candidates), compset, s, z, w,
                                         cfg.refineTopStructures);
        candidates = refineTopCandidates(std::move(candidates), compset, s, z, w,
                                         cfg.refineTopStructures);
    }
    int nRefined = 0;
    for (const auto& c : candidates)
        if (c.refined) ++nRefined;
    auto t2 = std::chrono::steady_clock::now();

    std::vector<EquivalenceClass> classes =
        rankAndCluster(std::move(candidates), compset, f, cfg.clusterTop, cfg.equivTol);
    auto t3 = std::chrono::steady_clock::now();

    auto secs = [](std::chrono::steady_clock::time_point a,
                   std::chrono::steady_clock::time_point b) {
        return std::chrono::duration<double>(b - a).count();
    };

    IdentifyResult out{
        std::move(classes),
        compset,
        state.nTotal,
        (int)survivors.size(),
        (int)enumerateStructures(compset.n(), cfg.allowDead).size(),
        nRefined,
        secs(t0, t3),
        secs(t0, t1),
        secs(t1, t2),
        secs(t2, t3)};
    return out;
}

}  // namespace ng
