#pragma once
// Synthetic DUT suite — port of rlc_id/synthetic.py (DESIGN.md section 8.2).

#include "circuits.hpp"
#include "linalg.hpp"

#include <string>
#include <vector>

namespace rlc {

constexpr double kFMin = 10.0;
constexpr double kFMax = 10e6;
constexpr int kNPoints = 30;
constexpr double kDefaultSigmaRel = 0.005;
constexpr uint64_t kDefaultSeed = 0;

struct DUT {
    std::string name;
    std::string group;
    TreePtr tree;
    std::vector<double> values;  // linear element values in canonical leaf order

    std::vector<double> theta() const {
        std::vector<double> t(values.size());
        for (size_t i = 0; i < values.size(); ++i) t[i] = std::log10(values[i]);
        return t;
    }
    std::vector<Complex> zExact(const std::vector<double>& f) const {
        std::vector<Complex> out(f.size());
        auto th = theta();
        evalThetaFreq(tree, th, f.data(), f.size(), out.data());
        return out;
    }
    std::string describe() const {
        auto th = theta();
        return toString(tree, &th);
    }
};

// the 12 synthetic DUTs of section 8.2
std::vector<DUT> makeDuts();

// 30 log-spaced frequencies from 10 Hz to 10 MHz
std::vector<double> defaultFrequencies();

struct Measurement {
    std::vector<double> f;
    std::vector<Complex> z;
};

// Simulated measurement: exact Z plus complex Gaussian noise (A3 model,
// sigma_k = sigma_rel * |z_k|, independent Re/Im).
Measurement measure(const DUT& dut, const std::vector<double>* f, double sigmaRel,
                    uint64_t seed);

// Max relative element-value error; requires the same canonical topology.
double maxParamError(const std::vector<double>& thetaFit, const DUT& dut);

}  // namespace rlc
