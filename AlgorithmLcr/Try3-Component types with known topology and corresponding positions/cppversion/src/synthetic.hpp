#pragma once
// Synthetic DUTs: named multigraphs + random cases + noisy measurements --
// port of topofit_id/synthetic.py.  Value conventions per original edge:
// R -> (r); C -> (c); L -> (l, rd), flat linear vectors follow the
// fit/nodal parameter order (per edge in order; L edges carry l then rd).

#include "graph.hpp"
#include "linalg.hpp"
#include "nodal.hpp"

#include <random>
#include <string>
#include <utility>
#include <vector>

namespace tf {

constexpr double kFMin = 10.0;
constexpr double kFMax = 10e6;
constexpr int kNPoints = 30;
constexpr double kDefaultSigmaRel = 0.005;

// true-value log-uniform ranges for random cases (inside PhysBounds)
struct RandomRanges {
    std::pair<double, double> r{0.0, 6.0};
    std::pair<double, double> c{-12.0, -6.0};
    std::pair<double, double> l{-8.0, 0.0};
    std::pair<double, double> rd{-3.0, 3.0};
};

std::vector<double> defaultFrequencies(int n = kNPoints);

struct DUT {
    std::string name;
    std::vector<std::tuple<int, int, char>> edges;
    std::vector<Value> values;  // per original edge, same order

    std::vector<double> flatValues() const;
    std::vector<Complex> zExact(const std::vector<double>& f) const;
};

std::vector<DUT> makeDuts();

// Simulate a measurement (Try1/Try2 noise model A3); mt19937_64 stream.
std::pair<std::vector<double>, std::vector<Complex>> measure(
    const DUT& dut, const std::vector<double>* f, double sigmaRel, uint64_t seed);

// Random connected multigraph DUT: Pruefer tree + extra parallel/cross
// edges, random kinds, log-uniform values in RandomRanges.
DUT randomCase(std::mt19937_64& rng, const std::string& name = "rand");

}  // namespace tf
