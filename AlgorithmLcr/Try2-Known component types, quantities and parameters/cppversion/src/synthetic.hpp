#pragma once
// Synthetic DUTs -- port of netgraph_id/synthetic.py.  Noise model follows
// Try1 (A3): complex Gaussian, Re/Im independent, sigma_k = sigma_rel |z_k|;
// default band 10 Hz .. 10 MHz, 30 log-spaced points.

#include "components.hpp"
#include "graph.hpp"
#include "nodal.hpp"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace ng {

constexpr double kFMin = 10.0;
constexpr double kFMax = 10e6;
constexpr int kNPoints = 30;

std::vector<double> defaultFrequencies(int n = kNPoints);

// Build a Network from (u, v, comp_index) triples (terminals 0/1; parallel
// triples legal).  The structure is canonicalized; the assignment follows
// the canonical instance order.
Network networkFromEdges(const ComponentSet& compset,
                         const std::vector<std::tuple<int, int, int>>& edges);

struct DUT {
    std::string name;
    std::string group;
    ComponentSet compset;
    Network network;
    std::vector<Complex> zExact(const std::vector<double>& f) const {
        return networkZ(network, compset, f);
    }
    std::string describe() const;
};

std::vector<DUT> makeDuts();

// Random admissible network: uniform V in [2, E+1] (or given), Pruefer tree
// + extra random edges, random component assignment.
Network randomNetwork(const ComponentSet& compset, std::mt19937_64& rng,
                      int V = -1, int maxTries = 1000);

// Noisy measurement z = z_exact + eps (mt19937_64 stream; the Python
// reference uses PCG64 -- same distribution, different stream).
std::vector<Complex> measureZ(const Network& network, const ComponentSet& compset,
                              const std::vector<double>& f, double sigmaRel,
                              uint64_t seed);

// Human-readable wiring string: "0-1:[R(1kohm)||C(100nF)] 0-2:[L(...)]".
std::string networkStr(const Network& network, const ComponentSet& compset);

}  // namespace ng
