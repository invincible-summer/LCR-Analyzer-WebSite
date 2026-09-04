#include "synthetic.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <queue>
#include <stdexcept>

namespace ng {

namespace {
constexpr double kPi = 3.14159265358979323846;

std::vector<double> logspaceF(double a, double b, int n) {
    std::vector<double> out((size_t)n);
    double la = std::log10(a), lb = std::log10(b);
    for (int i = 0; i < n; ++i)
        out[i] = std::pow(10.0, la + (lb - la) * (double)i / (double)(n - 1));
    return out;
}
}  // namespace

std::vector<double> defaultFrequencies(int n) { return logspaceF(kFMin, kFMax, n); }

Network networkFromEdges(const ComponentSet& compset,
                         const std::vector<std::tuple<int, int, int>>& edges) {
    (void)compset;  // edge indices already reference the canonical order
    if (edges.empty()) throw std::invalid_argument("need at least one edge");
    int V = 0;
    for (const auto& [u, v, ci] : edges) {
        (void)ci;
        V = std::max({V, u + 1, v + 1});
    }
    std::vector<int> mult = emptyMult(V);
    for (const auto& [u, v, ci] : edges) {
        if (u == v) throw std::invalid_argument("self loops are not allowed");
        mult[slotIndex(V, std::min(u, v), std::max(u, v))] += 1;
    }
    std::vector<int> cm = canonicalMult(V, mult);
    const auto& perms = permGroup(V);
    std::vector<int> pInv;
    for (const auto& p : perms) {
        if (permuteMult(V, mult, p) == cm) {
            pInv = p;
            break;
        }
    }
    if (pInv.empty()) throw std::runtime_error("edge mapping failed");
    // canonical edges in slot order, multiplicities from cm
    std::vector<std::pair<int, int>> canonEdges;
    const auto& slots = slotList(V);
    for (size_t k = 0; k < cm.size(); ++k)
        for (int q = 0; q < cm[k]; ++q) canonEdges.push_back(slots[k]);
    std::vector<int> assign;
    std::vector<char> used(edges.size(), 0);
    for (const auto& cp : canonEdges) {
        bool found = false;
        for (size_t t = 0; t < edges.size(); ++t) {
            if (used[t]) continue;
            auto [u, v, ci] = edges[t];
            int a = pInv[u], b = pInv[v];
            if (a > b) std::swap(a, b);
            if (std::make_pair(a, b) == cp) {
                assign.push_back(ci);
                used[t] = 1;
                found = true;
                break;
            }
        }
        if (!found) throw std::runtime_error("edge mapping failed");
    }
    return Network{makeStructure(V, cm, false), assign};
}

std::string DUT::describe() const { return networkStr(network, compset); }

std::vector<DUT> makeDuts() {
    std::vector<DUT> out;
    auto add = [&](const char* name, const char* group, ComponentSet cs,
                   std::vector<std::tuple<int, int, int>> edges) {
        out.push_back(DUT{name, group, std::move(cs),
                          networkFromEdges(cs, edges)});
    };
    // component indices refer to the ComponentSet canonical (sorted) order,
    // exactly as in netgraph_id/synthetic.py.
    add("dut1_series_RL", "series",
        ComponentSet::make({100.0}, {}, {{10e-3, 5.0}}),
        {{0, 2, 0}, {2, 1, 1}});
    add("dut2_parallel_RC", "parallel",
        ComponentSet::make({1e3}, {100e-9}, {}),
        {{0, 1, 0}, {0, 1, 1}});
    add("dut3_tank_RLC", "parallel",
        ComponentSet::make({1e3}, {10e-9}, {{100e-6, 0.0}}),
        {{0, 1, 0}, {0, 1, 1}, {0, 1, 2}});
    add("dut4_lpar", "series_parallel",
        ComponentSet::make({1.0}, {50e-12}, {{10e-6, 0.5}}),
        {{0, 2, 2}, {2, 1, 1}, {0, 1, 0}});
    add("dut5_cpar", "series",
        ComponentSet::make({0.05}, {10e-6}, {{2e-9, 0.0}}),
        {{0, 2, 0}, {2, 3, 1}, {3, 1, 2}});
    add("dut6_bridge", "bridge",
        ComponentSet::make({100.0, 470.0, 1e3}, {100e-9}, {{1e-3, 5.0}}),
        {{0, 2, 2}, {0, 3, 1}, {2, 1, 0}, {3, 1, 3}, {2, 3, 4}});
    add("dut7_double_L", "series_parallel",
        ComponentSet::make({}, {1e-6}, {{1e-3, 5.0}, {10e-3, 20.0}}),
        {{0, 2, 0}, {2, 1, 1}, {2, 1, 2}});
    add("dut8_ladder", "series_parallel",
        ComponentSet::make({100.0, 1e3}, {100e-9, 1e-6}, {}),
        {{0, 2, 0}, {2, 1, 2}, {2, 3, 1}, {3, 1, 3}});
    add("dut9_twin_R", "series_parallel",
        ComponentSet::make({10e3, 10e3}, {100e-9}, {}),
        {{0, 2, 0}, {2, 1, 1}, {0, 1, 2}});
    add("dut10_six_mixed", "mixed",
        ComponentSet::make({50.0, 200.0}, {10e-9, 1e-6}, {{100e-6, 2.0}, {1e-3, 10.0}}),
        {{0, 2, 0}, {2, 1, 2}, {0, 3, 1}, {3, 1, 3}, {2, 3, 4}, {0, 1, 5}});
    return out;
}

namespace {

// Uniform random labelled tree on 0..nV-1 via a Pruefer sequence.
std::vector<std::pair<int, int>> randomTree(int nV, std::mt19937_64& rng) {
    if (nV <= 2) {
        if (nV == 2) return std::vector<std::pair<int, int>>{std::make_pair(0, 1)};
        return {};
    }
    std::vector<int> prufer((size_t)nV - 2);
    for (auto& v : prufer)
        v = (int)(rng() % (uint64_t)nV);
    std::vector<int> deg((size_t)nV, 1);
    for (int p : prufer) deg[p] += 1;
    std::priority_queue<int, std::vector<int>, std::greater<int>> leaves;
    for (int i = 0; i < nV; ++i)
        if (deg[i] == 1) leaves.push(i);
    std::vector<std::pair<int, int>> edges;
    for (int p : prufer) {
        int leaf = leaves.top();
        leaves.pop();
        edges.push_back({std::min(leaf, p), std::max(leaf, p)});
        deg[leaf] -= 1;
        deg[p] -= 1;
        if (deg[p] == 1) leaves.push(p);
    }
    int a = leaves.top();
    leaves.pop();
    int b = leaves.top();
    edges.push_back({std::min(a, b), std::max(a, b)});
    return edges;
}

}  // namespace

Network randomNetwork(const ComponentSet& compset, std::mt19937_64& rng, int V,
                      int maxTries) {
    const int E = compset.n();
    int nV = (V > 0) ? V : (int)(rng() % (uint64_t)(E + 1 - 2 + 1)) + 2;  // U{2..E+1}
    nV = std::max(2, std::min(nV, E + 1));
    for (int attempt = 0; attempt < maxTries; ++attempt) {
        std::vector<std::pair<int, int>> pairs = randomTree(nV, rng);
        std::vector<std::pair<int, int>> allPairs;
        for (int i = 0; i < nV; ++i)
            for (int j = i + 1; j < nV; ++j) allPairs.push_back({i, j});
        for (int q = 0; q < E - (nV - 1); ++q)
            pairs.push_back(allPairs[rng() % allPairs.size()]);
        std::vector<int> mult = emptyMult(nV);
        for (auto& [u, v] : pairs)
            mult[slotIndex(nV, std::min(u, v), std::max(u, v))] += 1;
        if (!isConnected(nV, mult)) continue;
        if (hasDeadPart(nV, mult)) continue;
        std::vector<int> idx((size_t)E);
        for (int i = 0; i < E; ++i) idx[i] = i;
        for (size_t i = idx.size(); i > 1; --i) {
            size_t j = (size_t)(rng() % i);
            std::swap(idx[i - 1], idx[j]);
        }
        std::vector<std::tuple<int, int, int>> edges;
        for (int t = 0; t < (int)pairs.size(); ++t)
            edges.push_back({pairs[t].first, pairs[t].second, idx[t]});
        return networkFromEdges(compset, edges);
    }
    throw std::runtime_error("randomNetwork: no admissible graph found");
}

std::vector<Complex> measureZ(const Network& network, const ComponentSet& compset,
                              const std::vector<double>& f, double sigmaRel,
                              uint64_t seed) {
    std::vector<Complex> z0 = networkZ(network, compset, f);
    if (sigmaRel <= 0.0) return z0;
    std::mt19937_64 eng(seed);
    std::normal_distribution<double> norm(0.0, 1.0);
    for (auto& zk : z0) {
        double nr = norm(eng), ni = norm(eng);
        zk += sigmaRel * std::abs(zk) * Complex(nr, ni);
    }
    return z0;
}

std::string networkStr(const Network& network, const ComponentSet& compset) {
    const auto& comps = compset.components();
    std::string out;
    int t0 = 0;
    const auto& slots = slotList(network.structure.V);
    for (size_t k = 0; k < network.structure.mult.size(); ++k) {
        int m = network.structure.mult[k];
        if (m == 0) continue;
        auto [i, j] = slots[k];
        char head[32];
        std::snprintf(head, sizeof(head), "%d-%d:", i, j);
        out += head;
        for (int q = 0; q < m; ++q) {
            if (q) out += "||";
            out += comps[network.assign[t0 + q]].label();
        }
        out += " ";
        t0 += m;
    }
    if (!out.empty()) out.pop_back();
    return out;
}

}  // namespace ng
