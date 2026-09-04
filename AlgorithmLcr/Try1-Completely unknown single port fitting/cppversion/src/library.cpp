#include "library.hpp"

#include <algorithm>
#include <functional>
#include <mutex>
#include <set>
#include <tuple>

namespace rlc {

namespace {

const TreePtr& leafR() {
    static TreePtr t = Tree::makeLeaf('R');
    return t;
}
const TreePtr& leafL() {
    static TreePtr t = Tree::makeLeaf('L');
    return t;
}
const TreePtr& leafC() {
    static TreePtr t = Tree::makeLeaf('C');
    return t;
}
const std::vector<TreePtr>& theLeaves() {
    static std::vector<TreePtr> v{leafR(), leafL(), leafC()};
    return v;
}

// Leaf admissibility among the direct children of a node (R2' + R4):
//  - PAR: at most one leaf per kind EXCEPT L (parallel (L + Rd) devices are
//    second-order tanks, not mergeable);
//  - SER: all leaf kinds unique, and R must not coexist with L (R4: the
//    series R folds into the inductor's DC resistance).
bool childrenOk(NK kind, const std::vector<TreePtr>& children) {
    int mask = 0;  // bits: 0=R, 1=L, 2=C
    for (const auto& c : children) {
        if (!c->isLeaf) continue;
        int bit = 1 << (c->elem == 'R' ? 0 : c->elem == 'L' ? 1 : 2);
        if (kind == NK::Par && c->elem == 'L') continue;  // multi-L allowed
        if (mask & bit) return false;
        mask |= bit;
    }
    if (kind == NK::Ser) {
        return !((mask & 1) && (mask & 2));  // R4: no R together with L
    }
    return true;
}

struct GenCache {
    std::mutex mu;
    // key: (n, parent_kind, max_idepth) -> subtree options
    std::map<std::tuple<int, int, int>, std::vector<TreePtr>> subtreeOptions;
    // key: (n, max_idepth) -> trees of size n
    std::map<std::pair<int, int>, std::vector<TreePtr>> treesOfSize;
    // key: (max_n, max_idepth) -> cumulative library
    std::map<std::pair<int, int>, std::vector<TreePtr>> libraries;
};

GenCache& cache() {
    static GenCache c;
    return c;
}

std::vector<TreePtr> rootedTrees(int n, NK kind, int max_idepth);

// All canonical subtrees with `size` leaves attachable under a `parent_kind`
// node: leaves at size 1, otherwise opposite-kind roots within the depth
// budget.
std::vector<TreePtr> subtreeOptions(int size, NK parentKind, int max_idepth) {
    if (size == 1) return theLeaves();
    if (max_idepth < 1) return {};
    return rootedTrees(size, opposite(parentKind), max_idepth);
}

std::vector<TreePtr> rootedTrees(int n, NK kind, int max_idepth) {
    // options[size] = subtrees of that size attachable under this node
    std::vector<std::vector<TreePtr>> options;  // index size-1
    std::vector<bool> hasOption;
    for (int size = 1; size < n; ++size) {
        auto opts = subtreeOptions(size, kind, max_idepth - 1);
        hasOption.push_back(!opts.empty());
        options.push_back(std::move(opts));
    }

    std::vector<TreePtr> results;
    std::vector<TreePtr> chosen;

    std::function<void(int)> dfs = [&](int remaining) {
        if (remaining == 0) {
            if (chosen.size() >= 2 && childrenOk(kind, chosen)) {
                results.push_back(Tree::makeNode(kind, chosen));
            }
            return;
        }
        // candidate next child: canonical order >= previous (multiset order)
        std::string start;
        if (!chosen.empty()) start = canonical(chosen.back());
        for (int size = 1; size < n; ++size) {
            if (!hasOption[size - 1]) continue;
            if (size > remaining) continue;
            for (const auto& opt : options[size - 1]) {
                if (!start.empty() && canonical(opt) < start) continue;
                chosen.push_back(opt);
                dfs(remaining - size);
                chosen.pop_back();
            }
        }
    };
    dfs(n);

    // the multiset DFS is duplicate-free by construction; dedup defensively
    std::set<std::string> seen;
    std::vector<TreePtr> uniq;
    for (auto& t : results) {
        auto cs = canonical(t);
        if (seen.insert(cs).second) uniq.push_back(t);
    }
    return uniq;
}

std::vector<TreePtr> treesOfSizeUncached(int n, int max_idepth) {
    if (n == 1) return theLeaves();
    std::vector<TreePtr> out;
    for (NK kind : {NK::Ser, NK::Par}) {
        auto v = rootedTrees(n, kind, max_idepth);
        out.insert(out.end(), v.begin(), v.end());
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const TreePtr& a, const TreePtr& b) {
                         return canonical(a) < canonical(b);
                     });
    return out;
}

// caller must hold cache().mu
std::vector<TreePtr> treesOfSizeLocked(int n, int max_idepth) {
    auto& c = cache();
    auto key = std::make_pair(n, max_idepth);
    auto it = c.treesOfSize.find(key);
    if (it != c.treesOfSize.end()) return it->second;
    auto v = treesOfSizeUncached(n, max_idepth);
    return c.treesOfSize.emplace(key, std::move(v)).first->second;
}

}  // namespace

const std::vector<TreePtr>& TopologyLibrary::get(int max_n, int max_idepth) {
    auto& c = cache();
    std::lock_guard<std::mutex> lock(c.mu);
    auto key = std::make_pair(max_n, max_idepth);
    auto it = c.libraries.find(key);
    if (it != c.libraries.end()) return it->second;
    std::vector<TreePtr> out;
    for (int n = 1; n <= max_n; ++n) {
        auto v = treesOfSizeLocked(n, max_idepth);
        out.insert(out.end(), v.begin(), v.end());
    }
    return c.libraries.emplace(key, std::move(out)).first->second;
}

int TopologyLibrary::countOfSize(int n, int max_idepth) {
    auto& c = cache();
    std::lock_guard<std::mutex> lock(c.mu);
    return (int)treesOfSizeLocked(n, max_idepth).size();
}

std::vector<TreePtr> TopologyLibrary::ofSize(int n, int max_idepth) {
    if (n < 1) throw std::invalid_argument("device count must be >= 1");
    auto& c = cache();
    std::lock_guard<std::mutex> lock(c.mu);
    return treesOfSizeLocked(n, max_idepth);
}

void TopologyLibrary::clearCache() {
    auto& c = cache();
    std::lock_guard<std::mutex> lock(c.mu);
    c.subtreeOptions.clear();
    c.treesOfSize.clear();
    c.libraries.clear();
}

void warmLibraryCache(int max_n, int max_idepth) {
    TopologyLibrary::get(max_n, max_idepth);
}

}  // namespace rlc
