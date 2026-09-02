#pragma once
// Canonical series-parallel topology enumerator — port of rlc_id/library.py
// (DESIGN.md section 4.2 / appendix B.1).  Depth-limited generation with
// integer-partition DFS, alternating node kinds (R1), distinct leaf kinds per
// node (R2) and canonical child order (R3).

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
    static void clearCache();
};

// Warm the caches for every size/depth combination the tests will touch so
// that worker threads never race on cache fill.
void warmLibraryCache(int max_n, int max_idepth);

}  // namespace rlc
