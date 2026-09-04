#include "nodal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace ng {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

// Plain double LU with partial pivoting -- deliberately the same numerical
// policy as the numpy/LAPACK reference (both engines then carry *correlated*
// rounding errors, which largely cancel in cross-language comparisons of
// near-degenerate candidates; an extended-precision solve was measured to
// agree LESS because the reference's own double error no longer cancels).
bool luSolveComplex(std::vector<Complex>& A, int n, std::vector<Complex>& b) {
    for (int col = 0; col < n; ++col) {
        int piv = col;
        double best = std::abs(A[(size_t)col * n + col]);
        for (int r = col + 1; r < n; ++r) {
            double a = std::abs(A[(size_t)r * n + col]);
            if (a > best) {
                best = a;
                piv = r;
            }
        }
        if (!(best > 0.0) || !std::isfinite(best)) return false;
        if (piv != col) {
            for (int j = 0; j < n; ++j)
                std::swap(A[(size_t)piv * n + j], A[(size_t)col * n + j]);
            std::swap(b[piv], b[col]);
        }
        Complex d = A[(size_t)col * n + col];
        for (int r = col + 1; r < n; ++r) {
            Complex f = A[(size_t)r * n + col] / d;
            if (f == Complex(0.0, 0.0)) continue;
            A[(size_t)r * n + col] = f;
            for (int j = col + 1; j < n; ++j)
                A[(size_t)r * n + j] -= f * A[(size_t)col * n + j];
            b[r] -= f * b[col];
        }
    }
    for (int i = n - 1; i >= 0; --i) {
        Complex s = b[i];
        for (int j = i + 1; j < n; ++j) s -= A[(size_t)i * n + j] * b[j];
        b[i] = s / A[(size_t)i * n + i];
    }
    return true;
}

StructureStamps StructureStamps::build(const Structure& structure,
                                       const ComponentSet& compset) {
    return build(structure, compset.components());
}

StructureStamps StructureStamps::build(const Structure& structure,
                                       const std::vector<Component>& comps) {
    StructureStamps st;
    st.structure = structure;
    for (const auto& c : comps) {
        st.kinds.push_back(c.kind);
        st.vals.push_back(c.value);
        st.dcrs.push_back(c.dcr);
    }
    const auto soi = structure.slotOfInstances();
    for (int k = 0; k < (int)structure.mult.size(); ++k)
        if (structure.mult[k] > 0) {
            // first instance index of this slot (instances are contiguous)
            st.groupStarts.push_back(std::find(soi.begin(), soi.end(), k) - soi.begin());
            st.pairNodes.push_back(slotList(structure.V)[k]);
        }
    for (size_t p = 0; p < st.pairNodes.size(); ++p) {
        auto [i, j] = st.pairNodes[p];
        if (i > 0) st.diagTargets.push_back({(int)p, i - 1});
        if (j > 0) st.diagTargets.push_back({(int)p, j - 1});
        if (i > 0 && j > 0) st.offdiag.push_back({(int)p, i - 1, j - 1});
    }
    return st;
}

std::vector<Complex> StructureStamps::zBatch(
    const std::vector<std::vector<int>>& assigns, Complex s) const {
    const int N = (int)assigns.size();
    const int k = structure.V - 1;
    const int E = (int)kinds.size();
    const int P = (int)pairNodes.size();
    const Complex infZ(std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::infinity());

    std::vector<Complex> yComp((size_t)E);
    for (int e = 0; e < E; ++e) {
        if (kinds[e] == 'R') yComp[e] = Complex(1.0 / vals[e], 0.0);
        else if (kinds[e] == 'C') yComp[e] = s * vals[e];
        else yComp[e] = 1.0 / (Complex(dcrs[e], 0.0) + s * vals[e]);
    }

    std::vector<Complex> out((size_t)N);
    std::vector<Complex> yPair((size_t)P), diag((size_t)k), Y((size_t)k * k), b((size_t)k);
    for (int n = 0; n < N; ++n) {
        // per-slot admittance = sum of instance admittances (instance order)
        const auto& a = assigns[n];
        for (int p = 0; p < P; ++p) {
            Complex sum(0.0, 0.0);
            int start = groupStarts[p];
            int end = (p + 1 < P) ? groupStarts[p + 1] : (int)a.size();
            for (int t = start; t < end; ++t) sum += yComp[a[t]];
            yPair[p] = sum;
        }
        std::fill(diag.begin(), diag.end(), Complex(0.0, 0.0));
        for (const auto& [p, r] : diagTargets) diag[r] += yPair[p];
        std::fill(Y.begin(), Y.end(), Complex(0.0, 0.0));
        for (int r = 0; r < k; ++r) Y[(size_t)r * k + r] = diag[r];
        for (const auto& [p, ri, rj] : offdiag) {
            Y[(size_t)ri * k + rj] = -yPair[p];
            Y[(size_t)rj * k + ri] = -yPair[p];
        }
        std::fill(b.begin(), b.end(), Complex(0.0, 0.0));
        b[0] = Complex(1.0, 0.0);
        if (!luSolveComplex(Y, k, b)) {
            out[n] = infZ;
            continue;
        }
        out[n] = b[0];
    }
    return out;
}

std::vector<std::vector<Complex>> StructureStamps::zFull(
    const std::vector<std::vector<int>>& assigns,
    const std::vector<Complex>& sArray) const {
    std::vector<std::vector<Complex>> out(assigns.size(),
                                          std::vector<Complex>(sArray.size()));
    std::vector<std::vector<int>> one(1);
    for (size_t m = 0; m < sArray.size(); ++m) {
        std::vector<Complex> zc = zBatch(assigns, sArray[m]);
        for (size_t c = 0; c < assigns.size(); ++c) out[c][m] = zc[c];
    }
    (void)one;
    return out;
}

std::vector<Complex> networkZ(const Network& network, const ComponentSet& compset,
                              const std::vector<double>& f) {
    return networkZValues(network, compset.components(), f);
}

std::vector<Complex> networkZValues(const Network& network,
                                    const std::vector<Component>& comps,
                                    const std::vector<double>& f) {
    StructureStamps stamps = StructureStamps::build(network.structure, comps);
    std::vector<Complex> s(f.size());
    for (size_t k = 0; k < f.size(); ++k)
        s[k] = Complex(0.0, 1.0) * (2.0 * kPi * f[k]);
    return stamps.zFull({network.assign}, s)[0];
}

// ---------------------------------------------------------------------------
// DC / HF asymptotic invariants (pure graph + real linear solve)
// ---------------------------------------------------------------------------

namespace {

double effectiveResistance(const std::vector<std::tuple<int, int, double>>& edges,
                           int t0, int t1) {
    // Driving-point resistance of a small resistor multigraph; terminals must
    // be touched by some edge, else open (inf).
    std::set<int> touched;
    for (const auto& e : edges) {
        touched.insert(std::get<0>(e));
        touched.insert(std::get<1>(e));
    }
    if (!touched.count(t0) || !touched.count(t1))
        return std::numeric_limits<double>::infinity();
    std::map<int, int> labels;
    for (int n : touched) labels.emplace(n, (int)labels.size());
    const int n = (int)labels.size();
    std::vector<double> Y((size_t)n * n, 0.0);
    for (const auto& e : edges) {
        double g = 1.0 / std::get<2>(e);
        int i = labels[std::get<0>(e)], j = labels[std::get<1>(e)];
        Y[(size_t)i * n + i] += g;
        Y[(size_t)j * n + j] += g;
        Y[(size_t)i * n + j] -= g;
        Y[(size_t)j * n + i] -= g;
    }
    if (t0 == t1) return 0.0;
    int r0 = labels[t0], r1 = labels[t1];
    const int k = n - 1;
    std::vector<double> Yr((size_t)k * k, 0.0);
    std::vector<int> keep;
    for (int i = 0; i < n; ++i)
        if (i != r0) keep.push_back(i);
    for (int a = 0; a < k; ++a)
        for (int b = 0; b < k; ++b) Yr[(size_t)a * k + b] = Y[(size_t)keep[a] * n + keep[b]];
    std::vector<double> rhs(k, 0.0);
    int c1 = -1;
    for (int a = 0; a < k; ++a)
        if (keep[a] == r1) c1 = a;
    rhs[c1] = 1.0;
    // real LU with partial pivoting
    for (int col = 0; col < k; ++col) {
        int piv = col;
        double best = std::fabs(Yr[(size_t)col * k + col]);
        for (int r = col + 1; r < k; ++r) {
            double a = std::fabs(Yr[(size_t)r * k + col]);
            if (a > best) {
                best = a;
                piv = r;
            }
        }
        if (!(best > 0.0)) return std::numeric_limits<double>::infinity();
        if (piv != col) {
            for (int j = 0; j < k; ++j) std::swap(Yr[(size_t)piv * k + j], Yr[(size_t)col * k + j]);
            std::swap(rhs[piv], rhs[col]);
        }
        for (int r = col + 1; r < k; ++r) {
            double f = Yr[(size_t)r * k + col] / Yr[(size_t)col * k + col];
            if (f == 0.0) continue;
            for (int j = col; j < k; ++j) Yr[(size_t)r * k + j] -= f * Yr[(size_t)col * k + j];
            rhs[r] -= f * rhs[col];
        }
    }
    for (int i = k - 1; i >= 0; --i) {
        for (int j = i + 1; j < k; ++j) rhs[i] -= Yr[(size_t)i * k + j] * rhs[j];
        rhs[i] /= Yr[(size_t)i * k + i];
    }
    return rhs[c1];
}

}  // namespace

double asymptoteImpedance(const Network& network, const ComponentSet& compset, bool dc) {
    const auto& comps = compset.components();
    const int V = network.structure.V;
    const auto& slots = slotList(V);
    const auto soi = network.structure.slotOfInstances();

    std::vector<int> parent((size_t)V);
    std::iota(parent.begin(), parent.end(), 0);
    auto find = [&parent](int a) {
        while (parent[a] != a) {
            parent[a] = parent[parent[a]];
            a = parent[a];
        }
        return a;
    };
    auto unite = [&find, &parent](int a, int b) {
        int ra = find(a), rb = find(b);
        if (ra != rb) parent[ra] = rb;
    };

    std::vector<std::tuple<int, int, double>> resistive;
    for (size_t t = 0; t < soi.size(); ++t) {
        const Component& c = comps[network.assign[t]];
        auto [u, v] = slots[soi[t]];
        if (dc) {
            if (c.kind == 'C') continue;               // open
            if (c.kind == 'L' && c.dcr == 0.0) {       // short
                unite(u, v);
                continue;
            }
            double r = (c.kind == 'R') ? c.value : c.dcr;
            resistive.push_back({u, v, r});
        } else {
            if (c.kind == 'L') continue;               // open
            if (c.kind == 'C') {                       // short
                unite(u, v);
                continue;
            }
            resistive.push_back({u, v, c.value});
        }
    }
    if (find(0) == find(1)) return 0.0;
    std::vector<std::tuple<int, int, double>> merged;
    for (auto& e : resistive) {
        int u = find(std::get<0>(e)), v = find(std::get<1>(e));
        if (u == v) continue;  // zero-resistance loop through shorts
        merged.push_back({u, v, std::get<2>(e)});
    }
    return effectiveResistance(merged, find(0), find(1));
}

}  // namespace ng
