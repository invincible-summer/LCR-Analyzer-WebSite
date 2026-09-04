#include "synthetic.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

namespace tf {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

std::vector<double> defaultFrequencies(int n) {
    std::vector<double> out((size_t)n);
    double la = std::log10(kFMin), lb = std::log10(kFMax);
    for (int i = 0; i < n; ++i)
        out[(size_t)i] = std::pow(10.0, la + (lb - la) * (double)i / (double)(n - 1));
    return out;
}

std::vector<double> DUT::flatValues() const {
    std::vector<double> out;
    out.reserve(values.size() + 8);
    for (const Value& v : values) {
        out.push_back(v.v1);
        if (v.kind == 'L') out.push_back(v.v2);
    }
    return out;
}

std::vector<Complex> DUT::zExact(const std::vector<double>& f) const {
    NodalModel model = NodalModel::fromEdges(edges);
    std::vector<Complex> s(f.size());
    for (size_t k = 0; k < f.size(); ++k)
        s[k] = Complex(0.0, 1.0) * (2.0 * kPi * f[k]);
    return model.zLinear(flatValues(), s);
}

std::vector<DUT> makeDuts() {
    std::vector<DUT> out;
    auto add = [&](const char* name,
                   std::vector<std::tuple<int, int, char>> edges,
                   std::vector<std::vector<double>> raw) {
        DUT d;
        d.name = name;
        d.edges = edges;
        for (size_t i = 0; i < edges.size(); ++i) {
            char k = std::get<2>(edges[i]);
            if (k == 'L') d.values.push_back(Value{'L', raw[i][0], raw[i][1]});
            else d.values.push_back(Value{k, raw[i][0], 0.0});
        }
        out.push_back(std::move(d));
    };
    add("ser_rc", {{0, 2, 'R'}, {2, 1, 'C'}}, {{1e3}, {100e-9}});
    add("par_rlc", {{0, 1, 'R'}, {0, 1, 'C'}, {0, 1, 'L'}},
        {{1e3}, {10e-9}, {1e-3, 1.0}});
    add("ind_parasitic", {{0, 2, 'R'}, {2, 1, 'L'}, {0, 1, 'C'}},
        {{1.0}, {10e-6, 0.5}, {50e-12}});
    add("ladder", {{0, 2, 'L'}, {2, 1, 'C'}, {2, 1, 'R'}},
        {{1e-3, 2.0}, {100e-9}, {10e3}});
    add("bridge", {{0, 2, 'R'}, {0, 3, 'R'}, {2, 1, 'R'}, {3, 1, 'R'}, {2, 3, 'C'}},
        {{1e3}, {2e3}, {3e3}, {4e3}, {10e-9}});
    add("reducible", {{0, 2, 'R'}, {2, 1, 'R'}, {2, 3, 'C'}},
        {{100.0}, {220.0}, {1e-9}});
    add("par_rc", {{0, 1, 'R'}, {0, 1, 'C'}}, {{1e3}, {10e-9}});
    add("par_ll", {{0, 1, 'L'}, {0, 1, 'L'}}, {{1e-3, 1.0}, {100e-6, 10.0}});
    add("ser_rl_absorb", {{0, 2, 'R'}, {2, 1, 'L'}}, {{50.0}, {1e-3, 2.0}});
    add("cap_parasitic", {{0, 2, 'R'}, {2, 3, 'L'}, {3, 1, 'C'}},
        {{0.05}, {2e-9, 0.01}, {10e-6}});
    add("double_tank", {{0, 2, 'L'}, {2, 3, 'L'}, {2, 3, 'C'}, {3, 1, 'C'}},
        {{1e-5, 0.2}, {1e-4, 0.5}, {1e-9}, {10e-9}});
    add("nested_red", {{4, 1, 'R'}, {4, 1, 'R'}, {0, 4, 'R'}, {4, 5, 'C'}},
        {{1e3}, {2e3}, {300.0}, {1e-9}});
    return out;
}

std::pair<std::vector<double>, std::vector<Complex>> measure(
    const DUT& dut, const std::vector<double>* f, double sigmaRel, uint64_t seed) {
    std::vector<double> freq = f ? *f : defaultFrequencies();
    std::vector<Complex> z = dut.zExact(freq);
    if (sigmaRel > 0) {
        std::mt19937_64 eng(seed);
        std::normal_distribution<double> norm(0.0, 1.0);
        for (auto& zk : z) {
            double nr = norm(eng), ni = norm(eng);
            zk += sigmaRel * std::abs(zk) * Complex(nr, ni);
        }
    }
    return {freq, z};
}

namespace {

// Random labelled tree on 0..V-1 from a uniform Pruefer code.
std::vector<std::pair<int, int>> prueferTree(std::mt19937_64& rng, int V) {
    if (V == 1) return {};
    std::vector<int> code;
    if (V > 2) {
        code.resize((size_t)V - 2);
        for (auto& c : code) c = (int)(rng() % (uint64_t)V);
    }
    std::vector<int> degree((size_t)V, 1);
    for (int c : code) degree[(size_t)c] += 1;
    std::priority_queue<int, std::vector<int>, std::greater<int>> leaves;
    for (int n = 0; n < V; ++n)
        if (degree[(size_t)n] == 1) leaves.push(n);
    std::vector<std::pair<int, int>> edges;
    for (int c : code) {
        int leaf = leaves.top();
        leaves.pop();
        edges.push_back({leaf, c});
        degree[(size_t)leaf] -= 1;
        degree[(size_t)c] -= 1;
        if (degree[(size_t)c] == 1) leaves.push(c);
    }
    int a = leaves.top();
    leaves.pop();
    int b = leaves.top();
    edges.push_back({a, b});
    return edges;
}

}  // namespace

DUT randomCase(std::mt19937_64& rng, const std::string& name) {
    RandomRanges rr;
    int V = 2 + (int)(rng() % 5);  // U{2..6}
    std::vector<std::pair<int, int>> pairs = prueferTree(rng, V);
    int nExtra = (int)(rng() % 4);  // U{0..3}
    for (int q = 0; q < nExtra; ++q) {
        int u = (int)(rng() % (uint64_t)V), v = (int)(rng() % (uint64_t)V);
        if (u == v) continue;
        pairs.push_back({std::min(u, v), std::max(u, v)});
    }
    auto uniform = [&rng](double lo, double hi) {
        return lo + (double)(rng() % 1000000007ULL) / 1000000007.0 * (hi - lo);
    };
    DUT dut;
    dut.name = name;
    for (auto& [u, v] : pairs) {
        char k = "RCL"[(int)(rng() % 3)];
        dut.edges.push_back({u, v, k});
        if (k == 'R')
            dut.values.push_back(Value{'R', std::pow(10.0, uniform(rr.r.first, rr.r.second)), 0.0});
        else if (k == 'C')
            dut.values.push_back(Value{'C', std::pow(10.0, uniform(rr.c.first, rr.c.second)), 0.0});
        else
            dut.values.push_back(Value{'L', std::pow(10.0, uniform(rr.l.first, rr.l.second)),
                                       std::pow(10.0, uniform(rr.rd.first, rr.rd.second))});
    }
    return dut;
}

}  // namespace tf
