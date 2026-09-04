#include "enumerate.hpp"

#include <algorithm>
#include <map>
#include <mutex>
#include <numeric>
#include <set>
#include <stdexcept>

namespace ng {

namespace {

// All multisets of E indices from [0, S) in lexicographic (non-decreasing)
// order -- the combinations_with_replacement order.
void combosWithReplacement(int S, int E, const std::function<void(const std::vector<int>&)>& yield) {
    std::vector<int> combo((size_t)E, 0);
    yield(combo);
    if (E == 0) return;
    while (true) {
        // find rightmost element that can be incremented
        int i = E - 1;
        while (i >= 0 && combo[i] == S - 1) --i;
        if (i < 0) return;
        int v = combo[i] + 1;
        for (int k = i; k < E; ++k) combo[k] = v;
        yield(combo);
    }
}

std::vector<Structure> computeStructures(int E, bool allowDead) {
    if (E < 1) throw std::invalid_argument("E must be >= 1");
    std::vector<Structure> out;
    for (int V = 2; V <= E + 1; ++V) {
        const int S = nSlots(V);
        std::set<std::vector<int>> seen;
        combosWithReplacement(S, E, [&](const std::vector<int>& combo) {
            std::vector<int> mult = emptyMult(V);
            for (int k : combo) mult[k] += 1;
            if (!isConnected(V, mult)) return;
            if (!allowDead && hasDeadPart(V, mult)) return;
            std::vector<int> cm = canonicalMult(V, mult);
            if (seen.count(cm)) return;
            seen.insert(cm);
            out.push_back(makeStructure(V, cm, false));
        });
    }
    std::sort(out.begin(), out.end(), [](const Structure& a, const Structure& b) {
        if (a.V != b.V) return a.V < b.V;
        return a.mult < b.mult;
    });
    return out;
}

}  // namespace

const std::vector<Structure>& enumerateStructures(int E, bool allowDead) {
    static std::mutex mu;
    static std::map<std::pair<int, bool>, std::vector<Structure>> cache;
    std::lock_guard<std::mutex> lock(mu);
    auto key = std::make_pair(E, allowDead);
    auto it = cache.find(key);
    if (it == cache.end())
        it = cache.emplace(key, computeStructures(E, allowDead)).first;
    return it->second;
}

void iterAssignments(const Structure& structure, const ComponentSet& compset,
                     const std::function<void(const std::vector<int>&)>&& yield) {
    const auto& comps = compset.components();
    const int E = structure.nEdges();
    const int V = structure.V;
    if (E != compset.n())
        throw std::invalid_argument("structure edge count != component count");
    if (E == 0) return;

    const auto soi = structure.slotOfInstances();
    std::vector<std::vector<int>> instBySlot((size_t)nSlots(V));
    for (int t = 0; t < E; ++t) instBySlot[soi[t]].push_back(t);

    const bool useAut = structure.aut.size() > 1;
    std::set<std::vector<std::vector<Component>>> seen;

    std::vector<int> perm((size_t)E);
    std::iota(perm.begin(), perm.end(), 0);  // itertools.permutations order
    do {
        std::vector<std::vector<Component>> ser((size_t)nSlots(V));
        for (int slot = 0; slot < nSlots(V); ++slot) {
            const auto& occ = instBySlot[slot];
            if (occ.empty()) continue;
            auto& g = ser[slot];
            g.reserve(occ.size());
            for (int t : occ) g.push_back(comps[perm[t]]);
            std::sort(g.begin(), g.end());
        }
        if (useAut) {
            std::vector<std::vector<Component>> best = ser;
            for (const auto& p : structure.aut) {
                auto m = permuteSlotKeys(V, ser, p);
                if (m < best) best = std::move(m);
            }
            ser = std::move(best);
        }
        if (seen.count(ser)) continue;
        seen.insert(std::move(ser));
        yield(perm);
    } while (std::next_permutation(perm.begin(), perm.end()));
}

int countAssignments(const Structure& structure, const ComponentSet& compset) {
    int n = 0;
    iterAssignments(structure, compset, [&](const std::vector<int>&) { ++n; });
    return n;
}

}  // namespace ng
