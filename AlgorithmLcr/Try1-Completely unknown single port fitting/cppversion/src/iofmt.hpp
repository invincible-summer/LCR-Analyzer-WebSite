#pragma once
// Unified measurement input + Try1 device-count constraint — port of
// rlc_id/iofmt.py implementing ../../INPUT_FORMAT.md (section 1 and 2.1).
//
// measurements.txt: first line n, then exactly n lines "f Rz Iz" (frequency
// [Hz], Re(Z) [ohm], Im(Z) [ohm]).  '#' starts a comment (whole line or
// trailing), blank lines are ignored, fields are whitespace separated.
// Dump uses %.17g so load(dump(x)) == x bit-for-bit.
//
// count.txt (optional): a single positive integer — the circuit's exact
// device count (an L with its series DCR counts as ONE device).

#include <complex>
#include <string>
#include <utility>
#include <vector>

namespace rlc {

struct Measurements {
    std::vector<double> f;                     // frequencies [Hz], > 0
    std::vector<std::complex<double>> z;       // impedance samples
};

// throws std::invalid_argument (with line/field context) on malformed input
Measurements parseMeasurements(const std::string& text);
Measurements loadMeasurements(const std::string& path);
std::string formatMeasurements(const std::vector<double>& f,
                               const std::vector<std::complex<double>>& z);

// count.txt: exactly one content line holding one positive integer
int parseCount(const std::string& text);
int loadCount(const std::string& path);
std::string formatCount(int n);

}  // namespace rlc
