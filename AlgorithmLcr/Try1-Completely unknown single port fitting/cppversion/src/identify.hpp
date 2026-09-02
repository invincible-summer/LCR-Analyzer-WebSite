#pragma once
// Public entry point — port of rlc_id/__init__.py: identify(f, z, weights,
// config) following the dual-engine flow of DESIGN.md appendix B.2.

#include "fit_engine_a.hpp"
#include "fit_engine_b.hpp"
#include "library.hpp"
#include "pruning.hpp"
#include "selector.hpp"
#include "synthetic.hpp"

#include <optional>
#include <vector>

namespace rlc {

struct Config {
    int maxN = 4;                       // engine-A library size limit (A1)
    int maxOrder = 4;                   // engine-B rational order scan limit
    int skIters = 15;
    bool enableF2 = true;
    bool enableF3 = true;
    int nStartsCoarse = 3;
    int nStartsRefine = 10;
    double refineFraction = 0.2;
    int maxIDepth = kDefaultMaxIDepth;
    uint64_t seed = 0;

    EngineAConfig engineAConfig() const {
        EngineAConfig c;
        c.nStartsCoarse = nStartsCoarse;
        c.nStartsRefine = nStartsRefine;
        c.refineFraction = refineFraction;
        c.seed = seed;
        return c;
    }
};

struct IdentifyResult {
    std::vector<EquivalenceClass> classes;
    AsymptoticFeatures features;
    RationalModel zModel;
    RationalModel yModel;
    std::vector<Candidate> foster;  // all Foster candidates incl. skipped (D8)
    int nLibrary = 0;
    int nPrunedKept = 0;

    const EquivalenceClass* best() const {
        return classes.empty() ? nullptr : &classes[0];
    }
};

IdentifyResult identify(const std::vector<double>& f, const std::vector<Complex>& z,
                        const std::vector<double>* weights = nullptr,
                        const Config* config = nullptr);

}  // namespace rlc
