#pragma once
// Unified inputs (../../../INPUT_FORMAT.md section 1 + 2.3) -- port of
// topofit_id/iofmt.py.
//
// measurements.txt: first line n, then exactly n lines "f Rz Iz".
// topology.txt: node count V, then the upper-triangle rows of edge counts
// (row i holds V-1-i integers), then the edge-type queue (one R|L|C per
// line, slot-major order).  '#' comments, blank lines ignored, %.17g dump.

#include "linalg.hpp"

#include <string>
#include <tuple>
#include <vector>

namespace tf {

struct Measurements {
    std::vector<double> f;
    std::vector<Complex> z;
};

Measurements parseMeasurements(const std::string& text);
Measurements loadMeasurements(const std::string& path);
std::string formatMeasurements(const std::vector<double>& f,
                               const std::vector<Complex>& z);

// edges = [(u, v, kind), ...] with u < v (INPUT_FORMAT.md sec 2.3).
std::vector<std::tuple<int, int, char>> parseTopology(const std::string& text);
std::vector<std::tuple<int, int, char>> loadTopology(const std::string& path);
std::string formatTopology(const std::vector<std::tuple<int, int, char>>& edges);

}  // namespace tf
