#pragma once
// Small dense numerical kernels used by the identification pipeline.
// Self-contained replacements for the numpy/scipy primitives the Python
// reference relies on: scaled linear least squares (one-sided Jacobi SVD),
// real-polynomial root finding (Durand-Kerner + Newton polish), degree-1
// polyfit, median/geomspace helpers and a seeded RNG with Latin-hypercube
// sampling.

#include <complex>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

namespace rlc {

// Column-scaled least squares min ||A x - y||, mirroring
// fit_engine_b._lstsq_scaled (numpy.linalg.lstsq with rcond=None).
// A is m x n row-major.
std::vector<double> lstsqScaled(const std::vector<double>& A,
                                const std::vector<double>& y, int m, int n);

// Solve an SPD system in place; returns false when not positive definite.
bool solveSPD(std::vector<double>& A /* n*n */, int n, std::vector<double>& b);

// Roots of a real-coefficient polynomial given highest-first coefficients.
// Mirrors numpy.roots (leading zeros stripped; empty when degree < 1).
std::vector<std::complex<double>> polyRoots(const std::vector<double>& coeffs);

// Coefficients (highest first) of prod (x - r_i) over complex roots,
// mirroring numpy.poly.
std::vector<std::complex<double>> polyFromRoots(
    const std::vector<std::complex<double>>& roots);

// Slope of the least-squares degree-1 polynomial fit (numpy.polyfit deg=1).
double polyfitSlope(const double* x, const double* y, int k);

double median(std::vector<double> v);

// numpy.geomspace equivalent (endpoints exact).
std::vector<double> geomspace(double a, double b, int n);

// Seeded 64-bit RNG with the two distributions used by the pipeline.
struct Rng {
    std::mt19937_64 eng;
    explicit Rng(uint64_t seed) : eng(seed) {}
    double uniform01() { return std::uniform_real_distribution<double>(0.0, 1.0)(eng); }
    double normal() { return std::normal_distribution<double>(0.0, 1.0)(eng); }
    uint64_t intBelow(uint64_t bound) { return std::uniform_int_distribution<uint64_t>(0, bound - 1)(eng); }
    void shuffle(std::vector<int>& v);
};

// Latin-hypercube starts over the [lb, ub] box (qmc.LatinHypercube analogue).
std::vector<std::vector<double>> lhsStarts(int n, const std::vector<double>& lb,
                                           const std::vector<double>& ub, Rng& rng);

}  // namespace rlc
