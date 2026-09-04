#pragma once
// Unified inputs (../../../INPUT_FORMAT.md section 1 + 2.2) -- port of
// netgraph_id/iofmt.py.
//
// measurements.txt: first line n, then exactly n lines "f Rz Iz".
// components.txt: one component per line "type parameter [dcr]" with no
// node information (the wiring is what the search must find).
// '#' starts a comment (whole line or trailing), blank lines are ignored.
// Dump uses %.17g so load(dump(x)) == x bit-for-bit.

#include "components.hpp"

#include <complex>
#include <string>
#include <vector>

namespace ng {

struct Measurements {
    std::vector<double> f;
    std::vector<std::complex<double>> z;
};

// throws std::invalid_argument (with line/field context) on malformed input
Measurements parseMeasurements(const std::string& text);
Measurements loadMeasurements(const std::string& path);
std::string formatMeasurements(const std::vector<double>& f,
                               const std::vector<std::complex<double>>& z);

ComponentSet parseComponents(const std::string& text);
ComponentSet loadComponents(const std::string& path);
std::string formatComponents(const ComponentSet& compset);

}  // namespace ng
