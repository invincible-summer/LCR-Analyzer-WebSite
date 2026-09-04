#pragma once
// Public entry point -- port of netgraph_id/__init__.py.
//
// identify(compset, f, z): exhaustive topology identification with known
// component values.  Pipeline: enumerate structures -> stream assignments
// -> probe funnel -> full-band evaluation -> ranking & clustering.

#include "components.hpp"
#include "filters.hpp"
#include "selector.hpp"

#include <chrono>
#include <vector>

namespace ng {

struct Config {
    int coarsePoints = 3;        // probe frequencies in the funnel
    double funnelRatio = 1e6;    // keep probe-RSS <= best * ratio
    int funnelMinKeep = 200;     // lower bound on funnel survivors
    int batchSize = 4096;        // candidates per batched nodal solve
    int clusterTop = 50;         // how many best candidates to cluster
    double equivTol = 1e-3;      // equivalence tolerance floor
    bool allowDead = false;      // include electrically dead structures
    int topK = 8;                // reported classes
};

struct IdentifyResult {
    std::vector<EquivalenceClass> classes;
    ComponentSet compset;
    long nCandidates = 0;
    int nFunnelKept = 0;
    int nStructures = 0;
    double elapsed = 0.0;
    double tFunnel = 0.0, tEval = 0.0, tCluster = 0.0;
    const EquivalenceClass* best() const {
        return classes.empty() ? nullptr : &classes[0];
    }
};

IdentifyResult identify(const ComponentSet& compset, const std::vector<double>& f,
                        const std::vector<Complex>& z,
                        const std::vector<double>* weights = nullptr,
                        const Config* config = nullptr);

}  // namespace ng
