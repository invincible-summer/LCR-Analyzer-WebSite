#pragma once
// Multi-start weighted complex NLS on a known topology -- port of
// topofit_id/fit.py (DESIGN.md sec.5).
//
// Pipeline per graph: exact topology reduction -> dual normalization
// (s_tilde = s/w0, z_tilde = z/z0) -> multi-start bounded least squares on
// log10 parameters with the exact adjoint Jacobian -> escalation restarts
// while wrmse > threshold (LHS / center LHS / perturbations / resonance
// pair restarts / damped homotopy / last-resort mixed rounds) -> polish ->
// diagnostics (elasticity weak flags, at-bound flags, SVD rank/cond).
//
// scipy least_squares(method="trf") is replaced by a box-constrained
// Levenberg-Marquardt with the same tolerance semantics (Try1 cppversion
// precedent); the random streams use mt19937_64 (same distributions as the
// PCG64/LatinHypercube reference, not the same stream).

#include "graph.hpp"
#include "linalg.hpp"
#include "metric.hpp"
#include "nodal.hpp"

#include <functional>
#include <string>
#include <vector>

namespace tf {

struct PhysBounds {
    double rLo = 1e-3, rHi = 1e7;
    double cLo = 1e-13, cHi = 1e-3;
    double lLo = 1e-10, lHi = 1e1;
    double rdLo = 1e-6, rdHi = 1e7;
};

struct FitConfig {
    int nStarts = 16;           // full-box LHS coarse starts (2x the
    int nCenter = 16;           // center-focused LHS (+-2 decades)
    int nPerturb = 12;          // Gaussian log-space restarts around best
    int nPolish = 3;            // tight re-fit of the best starts
    double tolCoarse = 1e-8;
    double tolPolish = 1e-13;
    int maxNfevFactor = 25;     // coarse max_nfev = max(120, factor * p)
    uint64_t seed = 0;
    int escalationRounds = 3;
    double escalationWrmse = 0.03;
    int lastResortRounds = 3;
    int lastResortBatch = 24;
    double visThreshold = 0.1;
};

struct GroupReport {
    int gid = 0;
    char kind = 'R';
    int u = 0, v = 0;
    std::vector<int> members;
    std::string mode;               // single | par | ser
    Value value;
    std::vector<std::string> weakParams;
    std::vector<std::string> atBound;
};

struct EdgeReport {
    int index = 0;
    char kind = 'R';
    std::string status;             // fitted | merged | dropped
    int group = -1;
    Value value;
    std::string note;
};

struct FitResult {
    std::vector<std::tuple<int, int, char>> edges;  // original input
    ReductionResult reduction;
    bool ok = false;
    double rss = 0.0;
    double wrmse = 0.0;
    double maxRel = 0.0;
    double aiccVal = 0.0;
    int nParams = 0;
    std::vector<GroupReport> groups;
    std::vector<EdgeReport> edgesOut;
    std::vector<double> thetaNorm;  // log10 normalized values, reduced order
    int nStartsUsed = 0;
    double seconds = 0.0;
    std::vector<double> jacSv;
    int jacRank = -1;
    double jacCond = std::numeric_limits<double>::infinity();
    NodalModel model;  // reduced model, for zModel()
    double w0 = 0.0, z0 = 0.0;

    std::vector<Value> groupValues() const;
    std::vector<Complex> zModel(const std::vector<double>& f) const;
    std::string describe() const;
};

// ---------------------------------------------------------------------------
// box-constrained Levenberg-Marquardt (replaces scipy trf; Try1 precedent)
// ---------------------------------------------------------------------------

struct LMOut {
    std::vector<double> x;
    std::vector<double> residual;
    double rss = 0.0;
    int nfev = 0;
};

LMOut lmFit(const std::function<void(const std::vector<double>&, std::vector<double>&)>& residual,
            const std::function<void(const std::vector<double>&, std::vector<double>&)>& jac,
            std::vector<double> x0, const std::vector<double>& lb,
            const std::vector<double>& ub, int maxNfev, double ftol, double xtol,
            double gtol);

// Fit one known-topology multigraph to (f, z); throws PortOpenError for a
// port-open graph.
FitResult fitGraph(const std::vector<double>& f, const std::vector<Complex>& z,
                   const std::vector<std::tuple<int, int, char>>& edges,
                   const FitConfig* config = nullptr);

inline FitResult identify(const std::vector<double>& f, const std::vector<Complex>& z,
                          const std::vector<std::tuple<int, int, char>>& edges,
                          const FitConfig* config = nullptr) {
    return fitGraph(f, z, edges, config);
}

// Fit several candidate topologies, ranked by AICc (ascending).
std::vector<FitResult> identifyMany(const std::vector<double>& f,
                                    const std::vector<Complex>& z,
                                    const std::vector<std::vector<std::tuple<int, int, char>>>& graphs,
                                    const FitConfig* config = nullptr);

}  // namespace tf
