#include "selector.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace ng {

std::vector<Candidate> evaluateCandidates(const std::vector<Network>& nets,
                                          const ComponentSet& compset,
                                          const std::vector<Complex>& s,
                                          const std::vector<Complex>& z,
                                          const std::vector<double>& w,
                                          int batchSize) {
    const int nObs = 2 * (int)z.size();
    const int p = compset.nParams();
    std::vector<Candidate> out;
    // group by structure, preserving first-appearance order (dict semantics)
    std::vector<std::pair<std::string, std::pair<Structure, std::vector<std::vector<int>>>>>
        byStructure;
    std::map<std::string, int> indexOf;
    for (const auto& net : nets) {
        std::string key = net.structure.key();
        auto it = indexOf.find(key);
        if (it == indexOf.end()) {
            indexOf[key] = (int)byStructure.size();
            byStructure.push_back({key, {net.structure, {net.assign}}});
        } else {
            byStructure[it->second].second.second.push_back(net.assign);
        }
    }
    for (auto& [key, entry] : byStructure) {
        const Structure& structure = entry.first;
        StructureStamps stamps = StructureStamps::build(structure, compset);
        bool spFlag = isSeriesParallel(structure.V, structure.mult);
        for (size_t i0 = 0; i0 < entry.second.size(); i0 += (size_t)batchSize) {
            size_t i1 = std::min(entry.second.size(), i0 + (size_t)batchSize);
            std::vector<std::vector<int>> chunk(entry.second.begin() + i0,
                                                entry.second.begin() + i1);
            std::vector<std::vector<Complex>> zModel = stamps.zFull(chunk, s);
            for (size_t row = 0; row < chunk.size(); ++row) {
                const std::vector<Complex>& zf = zModel[row];
                bool finite = true;
                for (const Complex& v : zf)
                    if (!std::isfinite(v.real()) || !std::isfinite(v.imag())) {
                        finite = false;
                        break;
                    }
                if (!finite) continue;
                std::vector<double> res = residualVector(z, zf, w);
                double rss = rssOf(res);
                auto [wrmse, mre] = fitMetrics(z, zf);
                Candidate c;
                c.network = Network{structure, chunk[row]};
                c.zFit = zf;
                c.rss = rss;
                c.aiccVal = aicc(rss, nObs, p);
                c.wrmse = wrmse;
                c.maxRelErr = mre;
                c.sp = spFlag;
                out.push_back(std::move(c));
            }
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const Candidate& a, const Candidate& b) { return a.rss < b.rss; });
    return out;
}

std::vector<double> makeValidationGrid(const std::vector<double>& f, int n,
                                       double expand) {
    double fmin = *std::min_element(f.begin(), f.end());
    double fmax = *std::max_element(f.begin(), f.end());
    // numpy.logspace over [log10(fmin/expand), log10(fmax*expand)]
    double la = std::log10(fmin / expand), lb = std::log10(fmax * expand);
    std::vector<double> out((size_t)n);
    for (int i = 0; i < n; ++i)
        out[i] = std::pow(10.0, la + (lb - la) * (double)i / (double)(n - 1));
    return out;
}

bool relDiffBelow(const std::vector<Complex>& za, const std::vector<Complex>& zb,
                  double tol) {
    double mx = -1.0;
    bool any = false;
    for (size_t k = 0; k < za.size(); ++k) {
        double denom = std::abs(za[k]);
        if (denom < 1e-300) denom = std::abs(zb[k]);
        double rel = std::abs(za[k] - zb[k]) / denom;
        if (!std::isfinite(rel)) continue;  // np.isfinite filter
        any = true;
        mx = std::max(mx, rel);
    }
    if (!any) return false;
    return mx < tol;
}

bool areEquivalent(const Network& a, const Network& b, const ComponentSet& compset,
                   const std::vector<double>& grid, double tol) {
    std::vector<Complex> za = networkZ(a, compset, grid);
    std::vector<Complex> zb = networkZ(b, compset, grid);
    return relDiffBelow(za, zb, tol);
}

namespace {

// str(network.serialize(comp_keys)) as the Python reference builds it --
// repr of the nested tuple (slot)(key)('C', value, dcr).  Reproduced here so
// the within-class representative choice matches the reference bit-for-bit
// (Python compares these strings, not the tuples).
std::string pySerialStr(const std::vector<std::vector<Component>>& serial) {
    auto keyStr = [](const Component& c) {
        return "('" + std::string(1, c.kind) + "', " + pyRepr(c.value) + ", " +
               pyRepr(c.dcr) + ")";
    };
    std::string out = "(";
    for (size_t k = 0; k < serial.size(); ++k) {
        if (k) out += ", ";
        out += "(";
        for (size_t q = 0; q < serial[k].size(); ++q) {
            if (q) out += ", ";
            out += keyStr(serial[k][q]);
        }
        if (serial[k].size() == 1) out += ",";
        out += ")";
    }
    if (serial.size() == 1) out += ",";
    out += ")";
    return out;
}

// (fewer internal junctions, series-parallel first, canonical wiring key)
struct SecondaryKey {
    int nInternal;
    int spPenalty;
    std::string serialStr;
    bool operator<(const SecondaryKey& o) const {
        if (nInternal != o.nInternal) return nInternal < o.nInternal;
        if (spPenalty != o.spPenalty) return spPenalty < o.spPenalty;
        return serialStr < o.serialStr;
    }
};

SecondaryKey secondaryKey(const Candidate& c, const std::vector<Component>& comps) {
    return SecondaryKey{c.nInternal(), c.sp ? 0 : 1, pySerialStr(c.network.serialize(comps))};
}

}  // namespace

std::vector<EquivalenceClass> rankAndCluster(std::vector<Candidate> candidates,
                                             const ComponentSet& compset,
                                             const std::vector<double>& f,
                                             int clusterTop, double equivTol) {
    if (candidates.empty()) return {};
    double sigmaHat = candidates[0].wrmse;  // candidates are RSS-sorted; py takes
    for (const auto& c : candidates)        // min(wrmse) -- same set, same min
        sigmaHat = std::min(sigmaHat, c.wrmse);
    double tol = std::max(equivTol, 3.0 * sigmaHat);
    std::vector<double> grid = makeValidationGrid(f);
    size_t topN = std::min((size_t)clusterTop, candidates.size());
    std::vector<std::vector<Complex>> zGrids(topN);
    for (size_t i = 0; i < topN; ++i)
        zGrids[i] = networkZ(candidates[i].network, compset, grid);

    std::vector<EquivalenceClass> classes;
    std::vector<std::vector<Complex>> classZ;
    for (size_t i = 0; i < topN; ++i) {
        bool matched = false;
        for (size_t ci = 0; ci < classes.size(); ++ci) {
            if (relDiffBelow(classZ[ci], zGrids[i], tol)) {
                classes[ci].members.push_back(std::move(candidates[i]));
                matched = true;
                break;
            }
        }
        if (!matched) {
            classes.push_back(EquivalenceClass{std::move(candidates[i]), {}});
            classZ.push_back(zGrids[i]);
        }
    }
    const auto& comps = compset.components();
    for (auto& cl : classes) {
        std::vector<Candidate> everyone;
        everyone.push_back(std::move(cl.representative));
        for (auto& m : cl.members) everyone.push_back(std::move(m));
        std::stable_sort(everyone.begin(), everyone.end(),
                         [&](const Candidate& a, const Candidate& b) {
                             return secondaryKey(a, comps) < secondaryKey(b, comps);
                         });
        cl.representative = std::move(everyone[0]);
        cl.members.clear();
        for (size_t i = 1; i < everyone.size(); ++i)
            cl.members.push_back(std::move(everyone[i]));
    }
    std::stable_sort(classes.begin(), classes.end(),
                     [](const EquivalenceClass& a, const EquivalenceClass& b) {
                         return a.rss() < b.rss();
                     });
    return classes;
}

}  // namespace ng
