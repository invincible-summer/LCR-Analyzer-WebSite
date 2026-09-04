#include "filters.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace ng {

long roundHalfEven(double x) {
    double f = std::floor(x);
    double d = x - f;
    if (d > 0.5) return (long)f + 1;
    if (d < 0.5) return (long)f;
    return (((long)f) % 2 == 0) ? (long)f : (long)f + 1;
}

std::vector<int> coarseIndices(int M, int nPoints) {
    if (M <= nPoints) {
        std::vector<int> out(M);
        for (int i = 0; i < M; ++i) out[i] = i;
        return out;
    }
    std::set<int> uq;
    uq.insert(0);
    uq.insert(M - 1);
    for (int i = 0; i < nPoints; ++i)
        uq.insert((int)roundHalfEven((double)i * (M - 1) / (nPoints - 1)));
    return std::vector<int>(uq.begin(), uq.end());
}

void FunnelState::update(const std::vector<Network>& nets,
                         const std::vector<double>& probeRss) {
    for (size_t i = 0; i < nets.size(); ++i) {
        nTotal += 1;
        if (probeRss[i] < bestProbe) bestProbe = probeRss[i];
    }
    nProbeEvaluated += (long)nets.size();
    double thr = bestProbe * funnelRatio;
    for (size_t i = 0; i < nets.size(); ++i)
        if (probeRss[i] <= thr) kept.push_back({probeRss[i], nets[i]});
}

std::vector<Network> FunnelState::finalKeep() const {
    double thr = bestProbe * funnelRatio;
    std::vector<Network> out;
    for (const auto& [prss, net] : kept)
        if (prss <= thr) out.push_back(net);
    if ((int)out.size() < minKeep) {
        std::vector<std::pair<double, Network>> sortedKept = kept;
        std::stable_sort(sortedKept.begin(), sortedKept.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        out.clear();
        for (int i = 0; i < minKeep && i < (int)sortedKept.size(); ++i)
            out.push_back(sortedKept[i].second);
    }
    return out;
}

FunnelState runFunnel(const ComponentSet& compset, const std::vector<Complex>& s,
                      const std::vector<Complex>& z, const std::vector<double>& w,
                      int coarsePoints, double funnelRatio, int minKeep, int batchSize,
                      bool allowDead) {
    const int E = compset.n();
    const auto& structures = enumerateStructures(E, allowDead);
    std::vector<int> probeIdx = coarseIndices((int)s.size(), coarsePoints);
    FunnelState state{compset, s, z, w, std::move(probeIdx), funnelRatio, minKeep,
                      batchSize, std::numeric_limits<double>::infinity(), {}, 0, 0};
    std::vector<Complex> sProbe, zProbe;
    std::vector<double> wProbe;
    for (int idx : state.probeIdx) {
        sProbe.push_back(s[idx]);
        zProbe.push_back(z[idx]);
        wProbe.push_back(w[idx]);
    }

    for (const auto& structure : structures) {
        StructureStamps stamps = StructureStamps::build(structure, compset);
        std::vector<std::vector<int>> batch;
        batch.reserve((size_t)state.batchSize);
        iterAssignments(structure, compset,
                        [&](const std::vector<int>& assign) {
                            batch.push_back(assign);
                            if ((int)batch.size() >= state.batchSize) {
                                std::vector<std::vector<Complex>> zm =
                                    stamps.zFull(batch, sProbe);
                                std::vector<double> prss =
                                    weightedRssBatch(zProbe, zm, wProbe);
                                std::vector<Network> nets;
                                nets.reserve(batch.size());
                                for (const auto& a : batch)
                                    nets.push_back(Network{structure, a});
                                state.update(nets, prss);
                                batch.clear();
                            }
                        });
        if (!batch.empty()) {
            std::vector<std::vector<Complex>> zm = stamps.zFull(batch, sProbe);
            std::vector<double> prss = weightedRssBatch(zProbe, zm, wProbe);
            std::vector<Network> nets;
            nets.reserve(batch.size());
            for (const auto& a : batch) nets.push_back(Network{structure, a});
            state.update(nets, prss);
        }
    }
    return state;
}

}  // namespace ng
