#pragma once
// Engine A: complex-domain weighted least squares over a topology library —
// port of rlc_id/fit_engine_a.py (DESIGN.md section 5).
//
// The Python version drives scipy.optimize.least_squares(method='trf'); this
// port replaces it with an equivalent box-constrained Levenberg-Marquardt
// optimizer (see lmFit) with the same tolerance semantics and the same
// multi-start two-stage coarse -> refine funnel (F4).

#include "circuits.hpp"
#include "linalg.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rlc {

// default relative-error weights w_k = 1/|z_k| (model A3)
std::vector<double> defaultWeights(const std::vector<Complex>& z);

// interleaved [Re r_1, Im r_1, ...] weighted residual
std::vector<double> residualVector(const TreePtr& tree, const std::vector<double>& theta,
                                   const std::vector<Complex>& s,
                                   const std::vector<Complex>& z,
                                   const std::vector<double>& w);

// exact Jacobian of the weighted residual wrt log10 parameters (2M x p,
// row-major; row 2k = Re dr_k, row 2k+1 = Im dr_k)
std::vector<double> jacobianCs(const TreePtr& tree, const std::vector<double>& theta,
                               const std::vector<Complex>& s,
                               const std::vector<double>& w);

double rssOf(const std::vector<double>& residual);

// corrected AIC (section 5.5); n_obs = 2M real equations, K = p + 1
double aicc(double rss, int n_obs, int p);

// (weighted) relative RMSE and max relative error (section 8.1)
std::pair<double, double> fitMetrics(const std::vector<Complex>& z,
                                     const std::vector<Complex>& zfit);

struct Candidate {
    TreePtr tree;
    std::vector<double> theta;
    double rss = 0.0;
    double aiccVal = 0.0;
    double wrmse = 0.0;
    double maxRelErr = 0.0;
    std::string engine = "A";
    std::string note;
    bool skipped = false;

    int nParams() const { return (int)theta.size(); }
    std::string canonicalStr() const { return canonical(tree); }
    std::vector<double> values() const;
};

// Data-driven start magnitudes (from pruning.py features)
struct StartHints {
    double rLevel = 1e3;
    double lEst = 1e-3;
    double cEst = 1e-8;
    bool hasWRes = false;
    double wRes = 0.0;    // angular frequency of an interior |Z| extremum
    bool hasRPeak = false;
    double rPeak = 0.0;   // max|Z| (parallel-resonance level)
};

std::vector<std::vector<double>> heuristicStarts(const TreePtr& tree,
                                                 const StartHints* hints);

// ---------------------------------------------------------------------------
// box-constrained nonlinear least squares (Levenberg-Marquardt)
// ---------------------------------------------------------------------------

struct LMOpts {
    int maxNfev = 0;          // 0 -> 100 * p (scipy trf default)
    double ftol = 1e-12;
    double xtol = 1e-12;
    double gtol = 1e-12;
};

struct LMOut {
    std::vector<double> x;
    std::vector<double> residual;
    double rss = 0.0;
    int nfev = 0;
};

// residual(theta) -> 2M vector; jac(theta) -> 2M x p row-major
LMOut lmFit(std::function<void(const std::vector<double>&, std::vector<double>&)> residual,
            std::function<void(const std::vector<double>&, std::vector<double>&)> jac,
            std::vector<double> x0, const std::vector<double>& lb,
            const std::vector<double>& ub, const LMOpts& opts);

// fit one topology from several starts; best-RSS candidate
std::optional<Candidate> fitTopology(const TreePtr& tree, const std::vector<Complex>& s,
                                     const std::vector<Complex>& z,
                                     const std::vector<double>& w,
                                     const std::vector<std::vector<double>>& starts,
                                     int maxNfev = 0, double tol = 1e-11);

struct EngineAConfig {
    int nStartsCoarse = 3;
    int nStartsRefine = 10;
    double refineFraction = 0.2;
    int maxNfevCoarse = 40;
    int maxNfevRefine = 0;  // 0 = unlimited (100*p)
    double tolCoarse = 1e-6;
    double tolRefine = 1e-11;
    uint64_t seed = 0;
};

// R7: outlier-robust refit of one candidate (IRLS pass); see the .cpp.
// Mutates the candidate in place when the robust fit improves it.
bool robustRefitCandidate(Candidate& c, const std::vector<Complex>& s,
                          const std::vector<Complex>& z,
                          const std::vector<double>& w);

// two-stage funnel (F4); extraStarts maps canonical strings to additional
// start vectors (engine-B Foster solutions)
std::vector<Candidate> fitLibrary(const std::vector<TreePtr>& trees,
                                  const std::vector<Complex>& s,
                                  const std::vector<Complex>& z,
                                  const std::vector<double>& w,
                                  const EngineAConfig& config,
                                  const StartHints* hints,
                                  const std::map<std::string, std::vector<std::vector<double>>>* extraStarts);

}  // namespace rlc
