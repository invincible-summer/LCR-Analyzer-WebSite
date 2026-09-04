#pragma once
// Canonical series-parallel topology enumerator — port of rlc_id/library.py
// (DESIGN.md section 4.2 / appendix B.1).  Depth-limited generation with
// integer-partition DFS, alternating node kinds (R1), the v2 leaf
// admissibility rules (R2' parallel multi-L + R4 series absorption) and
// canonical child order (R3).  Counts (depth <= 2, locked by tests):
//   n = 1..6 -> 3, 6, 22, 45, 87, 162   (depth 3 at n=4: 99).

#include "circuits.hpp"

#include <map>
#include <string>
#include <vector>

namespace rlc {

constexpr int kDefaultMaxIDepth = 2;

class TopologyLibrary {
public:
    // all canonical topologies with 1..max_n elements (cached per parameters)
    static const std::vector<TreePtr>& get(int max_n,
                                           int max_idepth = kDefaultMaxIDepth);
    // number of canonical topologies with exactly n elements
    static int countOfSize(int n, int max_idepth = kDefaultMaxIDepth);
    // the single n-device layer (the exact-count prior of Config::exactN);
    // returned by value (the internal cache stays authoritative)
    static std::vector<TreePtr> ofSize(int n, int max_idepth = kDefaultMaxIDepth);
    static void clearCache();
};

// Warm the caches for every size/depth combination the tests will touch so
// that worker threads never race on cache fill.
void warmLibraryCache(int max_n, int max_idepth);

}  // namespace rlc
