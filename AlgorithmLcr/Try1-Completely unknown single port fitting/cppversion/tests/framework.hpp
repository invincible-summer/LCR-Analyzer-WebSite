#pragma once
// Minimal dependency-free test harness shared by all suites.

#include "identify.hpp"
#include "report.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace rlctest {

using namespace rlc;

struct CaseResult {
    int index = -1;
    std::string name;
    bool ok = true;
    std::string detail;
};

struct TestCtx {
    std::string suite;
    std::vector<CaseResult> cases;
    long checks = 0;
    long checkFails = 0;
    CaseResult cur;
    bool inCase = false;

    void begin(const std::string& name) {
        cur = CaseResult{-1, name, true, ""};
        inCase = true;
    }
    void end() {
        if (inCase) {
            cases.push_back(cur);
            inCase = false;
        }
    }
    void check(bool cond, const std::string& what) {
        ++checks;
        if (!cond) {
            ++checkFails;
            if (inCase) {
                cur.ok = false;
                if (cur.detail.size() < 600) cur.detail += what + " | ";
            }
        }
    }
    void checkClose(double a, double b, double relTol, const std::string& what) {
        double denom = std::max(std::max(std::fabs(a), std::fabs(b)), 1e-300);
        bool ok = std::fabs(a - b) <= relTol * denom;
        ++checks;
        if (!ok) {
            ++checkFails;
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s: %.6g vs %.6g (rel %.3g > %.3g)", what.c_str(),
                          a, b, std::fabs(a - b) / denom, relTol);
            if (inCase) {
                cur.ok = false;
                if (cur.detail.size() < 600) cur.detail += std::string(buf) + " | ";
            }
        }
    }
    void addCase(CaseResult r) { cases.push_back(std::move(r)); }
};

// Run perCase(i) for i in [0, n) on nThreads workers; results returned in
// deterministic index order.
template <typename F>
std::vector<CaseResult> runParallel(int n, int nThreads, F&& perCase) {
    if (nThreads <= 1) {
        std::vector<CaseResult> out;
        out.reserve(n);
        for (int i = 0; i < n; ++i) out.push_back(perCase(i));
        return out;
    }
    std::atomic<int> next{0};
    std::vector<std::vector<CaseResult>> perThread(nThreads);
    std::vector<std::thread> pool;
    for (int t = 0; t < nThreads; ++t) {
        pool.emplace_back([&, t]() {
            while (true) {
                int i = next.fetch_add(1);
                if (i >= n) break;
                CaseResult r = perCase(i);
                r.index = i;
                perThread[t].push_back(std::move(r));
            }
        });
    }
    for (auto& th : pool) th.join();
    std::vector<CaseResult> out;
    out.reserve(n);
    for (int t = 0; t < nThreads; ++t)
        for (auto& r : perThread[t]) out.push_back(std::move(r));
    std::sort(out.begin(), out.end(),
              [](const CaseResult& a, const CaseResult& b) { return a.index < b.index; });
    return out;
}

// ---------------------------------------------------------------------------
// independent verification path: nodal analysis (MNA) of the same network
// ---------------------------------------------------------------------------

struct MnaElement {
    int a, b;
    char kind;
    double value;
    double dcr;  // series DC resistance of an L device (v2)
};

inline void mnaBuild(const Tree* t, const std::vector<double>& values, size_t& idx, int a,
                     int b, int& nextNode, std::vector<MnaElement>& out) {
    if (t->isLeaf) {
        if (t->elem == 'L') {  // real inductor: [L, Rd]
            out.push_back(MnaElement{a, b, 'L', values[idx], values[idx + 1]});
            idx += 2;
        } else {
            out.push_back(MnaElement{a, b, t->elem, values[idx], 0.0});
            idx += 1;
        }
        return;
    }
    if (t->kind == NK::Par) {
        for (const auto& c : t->kids) mnaBuild(c.get(), values, idx, a, b, nextNode, out);
        return;
    }
    int cur = a;
    for (size_t i = 0; i + 1 < t->kids.size(); ++i) {
        int nxt = nextNode++;
        mnaBuild(t->kids[i].get(), values, idx, cur, nxt, nextNode, out);
        cur = nxt;
    }
    mnaBuild(t->kids.back().get(), values, idx, cur, b, nextNode, out);
}

// Driving-point impedance at (node1, ground) with 1 A injected.
// long double: the reference must stay more precise than the tree
// evaluation under test even for extreme admittance ratios (10+ decades).
inline Complex mnaImpedance(const TreePtr& tree, const std::vector<double>& values, Complex s) {
    using LC = std::complex<long double>;
    LC sl((long double)s.real(), (long double)s.imag());
    std::vector<MnaElement> els;
    size_t idx = 0;
    int nextNode = 2;
    mnaBuild(tree.get(), values, idx, 1, 0, nextNode, els);
    int n = nextNode - 1;  // ground (node 0) eliminated
    std::vector<std::vector<LC>> Y(n, std::vector<LC>(n, LC(0.0L, 0.0L)));
    for (const auto& e : els) {
        LC y(0.0L, 0.0L);
        if (e.kind == 'R') {
            y = LC(1.0L / (long double)e.value, 0.0L);
        } else if (e.kind == 'L') {
            y = LC(1.0L, 0.0L) /
                (LC((long double)e.dcr, 0.0L) + sl * (long double)e.value);
        } else {
            y = sl * (long double)e.value;
        }
        int a = e.a - 1, b = e.b - 1;
        if (a >= 0) {
            Y[a][a] += y;
            if (b >= 0) {
                Y[a][b] -= y;
                Y[b][a] -= y;
            }
        }
        if (b >= 0) Y[b][b] += y;
    }
    // Gaussian elimination with partial pivoting; rhs = e1
    std::vector<LC> rhs(n, LC(0.0L, 0.0L));
    rhs[0] = LC(1.0L, 0.0L);
    for (int col = 0; col < n; ++col) {
        int piv = col;
        for (int r = col + 1; r < n; ++r)
            if (std::abs(Y[r][col]) > std::abs(Y[piv][col])) piv = r;
        if (std::abs(Y[piv][col]) == 0.0L) return Complex(0.0, 0.0);
        if (piv != col) {
            std::swap(Y[piv], Y[col]);
            std::swap(rhs[piv], rhs[col]);
        }
        for (int r = col + 1; r < n; ++r) {
            LC f = Y[r][col] / Y[col][col];
            for (int c2 = col; c2 < n; ++c2) Y[r][c2] -= f * Y[col][c2];
            rhs[r] -= f * rhs[col];
        }
    }
    std::vector<LC> V(n);
    for (int r = n - 1; r >= 0; --r) {
        LC acc = rhs[r];
        for (int c2 = r + 1; c2 < n; ++c2) acc -= Y[r][c2] * V[c2];
        V[r] = acc / Y[r][r];
    }
    return Complex((double)V[0].real(), (double)V[0].imag());
}

// ---------------------------------------------------------------------------
// sweep helpers
// ---------------------------------------------------------------------------

// max_k |Z1 - Z2| / |Z1| over a frequency grid
inline double maxRelDiffOnGrid(const TreePtr& t1, const std::vector<double>& th1,
                               const TreePtr& t2, const std::vector<double>& th2,
                               const std::vector<double>& f) {
    const size_t m = f.size();
    std::vector<Complex> z1(m), z2(m);
    evalThetaFreq(t1, th1, f.data(), m, z1.data());
    evalThetaFreq(t2, th2, f.data(), m, z2.data());
    double mx = 0.0;
    for (size_t k = 0; k < m; ++k) {
        double rel = std::abs(z1[k] - z2[k]) / std::max(std::abs(z1[k]), 1e-300);
        mx = std::max(mx, rel);
    }
    return mx;
}

// Rejection-sample an identifiable draw: every element must move |Z| by >=
// minSens (log-sensitivity) somewhere on the extended validation grid.
// Returns the best draw found and whether the identifiability bar was met.
inline std::vector<double> sampleIdentifiable(const TreePtr& tree, uint64_t seed,
                                              const std::vector<double>& f, bool& identifiable,
                                              double& minSensOut, int maxTries = 300) {
    std::vector<double> lb, ub;
    thetaBounds(tree, lb, ub);
    const size_t p = lb.size();  // parameters (two per L device)
    std::vector<double> grid = makeValidationGrid(f);
    const size_t g = grid.size();
    std::vector<Complex> s(g);
    for (size_t k = 0; k < g; ++k) s[k] = Complex(0.0, 1.0) * (2.0 * M_PI * grid[k]);

    Rng rng(seed);
    std::vector<double> bestTheta(p, 0.0);
    double bestSens = -1.0;
    for (int t = 0; t < maxTries; ++t) {
        std::vector<double> th(p);
        for (size_t i = 0; i < p; ++i) {
            double u = rng.uniform01();
            th[i] = (lb[i] + 0.5) + u * ((ub[i] - 1.0) - (lb[i] + 0.5));
        }
        std::vector<Complex> Z(g), J(p * g);
        evalJac(tree, th, s.data(), g, Z.data(), J.data());
        double minS = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < p; ++i) {
            double mx = 0.0;
            for (size_t k = 0; k < g; ++k) {
                double v = std::abs(J[i * g + k]) / (kLn10 * std::max(std::abs(Z[k]), 1e-300));
                mx = std::max(mx, v);
            }
            minS = std::min(minS, mx);
        }
        if (minS > bestSens) {
            bestSens = minS;
            bestTheta = th;
        }
        if (minS >= 0.01) break;
    }
    identifiable = bestSens >= 0.01;
    minSensOut = bestSens;
    return bestTheta;
}

// true when any node holds two children with identical canonical strings:
// the per-element parameter comparison is then ambiguous (branch swapping),
// so equivalence must be judged electrically instead
inline bool hasRepeatedSubtrees(const TreePtr& t) {
    if (t->isLeaf) return false;
    std::set<std::string> seen;
    for (const auto& c : t->kids) {
        if (!seen.insert(canonical(c)).second) return true;
    }
    for (const auto& c : t->kids)
        if (hasRepeatedSubtrees(c)) return true;
    return false;
}

// suite entry points (defined in the suite translation units)
void suiteCircuits(TestCtx& t);
void suiteLibrary(TestCtx& t);
void suiteEngineA(TestCtx& t);
void suiteEngineB(TestCtx& t);
void suitePruning(TestCtx& t);
void suiteSelector(TestCtx& t);
void suiteEndToEnd(TestCtx& t);
void suiteSweepN5(TestCtx& t);
void suiteSweepN6(TestCtx& t);
void suiteNoisySweep(TestCtx& t);
void suiteExtremesBands(TestCtx& t);
void suiteAdjacency(TestCtx& t);
void suiteIofmt(TestCtx& t);
void suiteExactN(TestCtx& t);

// EXACT / EQUIV / MISS classification of one identification run against a
// synthetic DUT (mirrors demo.py::classify).
inline std::string classifyDut(const DUT& dut, const IdentifyResult& res,
                               const std::vector<double>& f, double equivTol,
                               double& paramErr) {
    paramErr = -1.0;
    if (res.classes.empty()) return "MISS";
    const EquivalenceClass& best = res.classes[0];
    const Candidate& rep = best.representative;
    if (canonical(rep.tree) == canonical(dut.tree)) {
        paramErr = maxParamError(rep.theta, dut);
        return "EXACT";
    }
    Candidate truth;
    truth.tree = dut.tree;
    truth.theta = dut.theta();
    auto grid = makeValidationGrid(f);
    for (const auto& mem : best.members) {
        if (areEquivalent(mem, truth, grid, equivTol)) return "EQUIV";
    }
    return "MISS";
}

}  // namespace rlctest
