#include "graph.hpp"

#include <algorithm>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace ng {

namespace {

// Union-find with path halving over 0..V-1.
struct Dsu {
    std::vector<int> parent;
    explicit Dsu(int n) : parent((size_t)n) { std::iota(parent.begin(), parent.end(), 0); }
    int find(int a) {
        while (parent[a] != a) {
            parent[a] = parent[parent[a]];
            a = parent[a];
        }
        return a;
    }
    void unite(int a, int b) {
        int ra = find(a), rb = find(b);
        if (ra != rb) parent[ra] = rb;
    }
};

std::vector<std::pair<int, int>>& slotListMutable(int V) {
    static thread_local std::map<int, std::vector<std::pair<int, int>>> cache;
    auto it = cache.find(V);
    if (it == cache.end()) {
        std::vector<std::pair<int, int>> slots;
        for (int i = 0; i < V; ++i)
            for (int j = i + 1; j < V; ++j) slots.push_back({i, j});
        it = cache.emplace(V, std::move(slots)).first;
    }
    return it->second;
}

}  // namespace

const std::vector<std::pair<int, int>>& slotList(int V) { return slotListMutable(V); }

int nSlots(int V) { return V * (V - 1) / 2; }

int slotIndex(int V, int i, int j) {
    // row-major upper triangle: rows 0..i-1 hold V-1-a slots each, then
    // offset (j - i - 1) inside row i.
    return i * (V - 1) - i * (i - 1) / 2 + (j - i - 1);
}

std::vector<int> emptyMult(int V) { return std::vector<int>((size_t)nSlots(V), 0); }

std::vector<int> multDegree(int V, const std::vector<int>& mult) {
    std::vector<int> deg((size_t)V, 0);
    const auto& slots = slotList(V);
    for (size_t k = 0; k < slots.size(); ++k) {
        deg[slots[k].first] += mult[k];
        deg[slots[k].second] += mult[k];
    }
    return deg;
}

bool isConnected(int V, const std::vector<int>& mult) {
    Dsu dsu(V);
    const auto& slots = slotList(V);
    for (size_t k = 0; k < slots.size(); ++k) {
        if (mult[k] > 0) dsu.unite(slots[k].first, slots[k].second);
    }
    int root = dsu.find(0);
    for (int v = 0; v < V; ++v)
        if (dsu.find(v) != root) return false;
    return true;
}

bool hasDeadPart(int V, const std::vector<int>& mult) {
    if (V <= 2) return false;
    std::vector<std::vector<int>> adj((size_t)V);
    const auto& slots = slotList(V);
    for (size_t k = 0; k < slots.size(); ++k) {
        if (mult[k] > 0) {
            adj[slots[k].first].push_back(slots[k].second);
            adj[slots[k].second].push_back(slots[k].first);
        }
    }
    for (int c = 0; c < V; ++c) {
        std::vector<char> seen((size_t)V, 0);
        for (int start = 0; start < V; ++start) {
            if (start == c || seen[start]) continue;
            std::vector<int> stack{start};
            seen[start] = 1;
            bool hasTerminal = false;
            while (!stack.empty()) {
                int x = stack.back();
                stack.pop_back();
                if (x == 0 || x == 1) hasTerminal = true;
                for (int y : adj[x]) {
                    if (y != c && !seen[y]) {
                        seen[y] = 1;
                        stack.push_back(y);
                    }
                }
            }
            if (!hasTerminal) return true;
        }
    }
    return false;
}

bool structureOk(int V, const std::vector<int>& mult, bool allowDead) {
    if (!isConnected(V, mult)) return false;
    if (!allowDead && hasDeadPart(V, mult)) return false;
    return true;
}

std::vector<std::vector<int>> permGroup(int V) {
    std::vector<int> internal;
    for (int v = 2; v < V; ++v) internal.push_back(v);
    std::vector<std::vector<int>> out;
    out.reserve(2 * 1);  // expanded by permutation count below
    for (int swap = 0; swap < 2; ++swap) {
        std::vector<int> pi = internal;  // lexicographic order (itertools)
        do {
            std::vector<int> p((size_t)V);
            p[0] = swap ? 1 : 0;
            p[1] = swap ? 0 : 1;
            for (size_t k = 0; k < pi.size(); ++k) p[pi[k]] = (int)k + 2;
            out.push_back(std::move(p));
        } while (std::next_permutation(pi.begin(), pi.end()));
    }
    return out;
}

std::vector<int> permuteMult(int V, const std::vector<int>& mult,
                             const std::vector<int>& p) {
    const auto& slots = slotList(V);
    std::vector<int> out((size_t)nSlots(V), 0);
    for (size_t k = 0; k < slots.size(); ++k) {
        int a = p[slots[k].first], b = p[slots[k].second];
        if (a > b) std::swap(a, b);
        out[slotIndex(V, a, b)] += mult[k];
    }
    return out;
}

std::vector<int> canonicalMult(int V, const std::vector<int>& mult) {
    std::vector<int> best;
    for (const auto& p : permGroup(V)) {
        std::vector<int> m = permuteMult(V, mult, p);
        if (best.empty() || m < best) best = std::move(m);
    }
    return best;
}

std::vector<std::vector<int>> structureAutomorphisms(int V,
                                                     const std::vector<int>& mult) {
    std::vector<std::vector<int>> out;
    for (const auto& p : permGroup(V)) {
        if (permuteMult(V, mult, p) == mult) out.push_back(p);
    }
    return out;
}

std::string Structure::key() const {
    std::ostringstream ss;
    ss << V << ";";
    for (size_t k = 0; k < mult.size(); ++k) {
        if (k) ss << ",";
        ss << mult[k];
    }
    return ss.str();
}

std::string Structure::serialize() const {
    std::ostringstream ss;
    ss << "V" << V << ":";
    for (size_t k = 0; k < mult.size(); ++k) {
        if (k) ss << ",";
        ss << mult[k];
    }
    return ss.str();
}

std::vector<int> Structure::slotOfInstances() const {
    std::vector<int> out;
    out.reserve((size_t)nEdges());
    for (size_t k = 0; k < mult.size(); ++k)
        out.insert(out.end(), (size_t)mult[k], (int)k);
    return out;
}

Structure makeStructure(int V, std::vector<int> mult, bool canonicalize) {
    if ((int)mult.size() != nSlots(V))
        throw std::invalid_argument("mult has wrong number of slots for V");
    if (canonicalize) mult = canonicalMult(V, mult);
    std::vector<std::vector<int>> aut = structureAutomorphisms(V, mult);
    return Structure{V, std::move(mult), std::move(aut)};
}

std::vector<std::vector<Component>> permuteSlotKeys(
    int V, const std::vector<std::vector<Component>>& keysPerSlot,
    const std::vector<int>& p) {
    const auto& slots = slotList(V);
    std::vector<std::vector<Component>> out(keysPerSlot.size());
    for (size_t k = 0; k < slots.size(); ++k) {
        const auto& keys = keysPerSlot[k];
        if (keys.empty()) continue;
        int a = p[slots[k].first], b = p[slots[k].second];
        if (a > b) std::swap(a, b);
        auto& dst = out[slotIndex(V, a, b)];
        dst.insert(dst.end(), keys.begin(), keys.end());
    }
    for (auto& g : out) std::sort(g.begin(), g.end());
    return out;
}

std::vector<std::vector<Component>> Network::serialize(
    const std::vector<Component>& comps) const {
    const int V = structure.V;
    const auto soi = structure.slotOfInstances();
    std::vector<std::vector<Component>> perSlot((size_t)nSlots(V));
    for (size_t t = 0; t < assign.size(); ++t)
        perSlot[soi[t]].push_back(comps[assign[t]]);
    for (auto& g : perSlot) std::sort(g.begin(), g.end());
    if (structure.aut.size() <= 1) return perSlot;
    std::vector<std::vector<Component>> best = perSlot;
    for (const auto& p : structure.aut) {
        std::vector<std::vector<Component>> m = permuteSlotKeys(V, perSlot, p);
        if (m < best) best = std::move(m);
    }
    return best;
}

bool isSeriesParallel(int V, const std::vector<int>& mult) {
    if (V < 2) return false;
    // ordered count list over occupied slots (insertion order irrelevant to
    // the result; kept deterministic = slot order, new edges appended)
    std::vector<std::pair<std::pair<int, int>, int>> counts;
    auto findSlot = [&counts](int u, int v) -> int {
        for (size_t k = 0; k < counts.size(); ++k)
            if (counts[k].first == std::make_pair(u, v)) return (int)k;
        return -1;
    };
    const auto& slots = slotList(V);
    for (size_t k = 0; k < slots.size(); ++k)
        if (mult[k] > 0) counts.push_back({slots[k], mult[k]});

    std::vector<char> alive((size_t)V, 1);
    while (true) {
        // (a) parallel reduction: collapse every multi-edge to a single edge
        for (auto& kv : counts)
            if (kv.second > 1) kv.second = 1;
        // (b) one series reduction per pass
        std::vector<int> deg((size_t)V, 0);
        std::vector<std::vector<std::pair<int, int>>> inc((size_t)V);
        for (const auto& kv : counts) {
            int a = kv.first.first, b = kv.first.second, m = kv.second;
            deg[a] += m;
            deg[b] += m;
            for (int q = 0; q < m; ++q) {
                inc[a].push_back(kv.first);
                inc[b].push_back(kv.first);
            }
        }
        int target = -1;
        for (int x = 2; x < V; ++x) {
            if (alive[x] && deg[x] == 2) {
                target = x;
                break;
            }
        }
        if (target < 0) break;
        int x = target;
        std::pair<int, int> e1 = inc[x][0], e2 = inc[x][1];
        for (const auto& key : {e1, e2}) {
            int k = findSlot(key.first, key.second);
            counts[k].second -= 1;
        }
        for (const auto& key : {e1, e2}) {
            int k = findSlot(key.first, key.second);
            if (k >= 0 && counts[k].second <= 0) counts.erase(counts.begin() + k);
        }
        alive[x] = 0;
        int others[4] = {e1.first, e1.second, e2.first, e2.second};
        std::vector<int> uniq;
        for (int v : others) {
            if (v != x && std::find(uniq.begin(), uniq.end(), v) == uniq.end())
                uniq.push_back(v);
        }
        if (uniq.size() == 2) {
            int u = std::min(uniq[0], uniq[1]), v = std::max(uniq[0], uniq[1]);
            int k = findSlot(u, v);
            if (k >= 0) counts[k].second += 1;
            else counts.push_back({{u, v}, 1});
        }
        // uniq.size()==1: both edges went to the same partner -> dead pair
    }
    int portCount = 0;
    for (const auto& kv : counts)
        if (kv.first == std::make_pair(0, 1)) portCount = kv.second;
    return portCount == 1 && counts.size() == 1;
}

}  // namespace ng
