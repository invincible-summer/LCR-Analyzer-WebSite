#pragma once
// Numerical kernels for the Try3 pipeline (zero-dependency replacements for
// the numpy/scipy primitives the Python reference relies on): complex LU,
// singular values of a real matrix (one-sided Jacobi), median, a seeded RNG
// with normal noise and Latin-hypercube starts.

#include <complex>
#include <cstdint>
#include <random>
#include <vector>

namespace tf {

using Complex = std::complex<double>;

constexpr double kLn10 = 2.30258509299404568402;
// sanitized stand-in for non-finite Z (singular Y at an exact internal
// resonance); large enough to dominate any relative residual
constexpr double kBigZ = 1e12;

// LU with partial pivoting; returns false when exactly singular.
bool luSolveComplex(std::vector<Complex>& A, int n, std::vector<Complex>& b);

// Singular values of A (m x n row-major, m >= n), descending.
std::vector<double> svdValues(const std::vector<double>& A, int m, int n);

double median(std::vector<double> v);

struct Rng {
    std::mt19937_64 eng;
    explicit Rng(uint64_t seed) : eng(seed) {}
    double uniform01() { return std::uniform_real_distribution<double>(0.0, 1.0)(eng); }
    double normal() { return std::normal_distribution<double>(0.0, 1.0)(eng); }
    int intBelow(int bound) { return (int)(eng() % (uint64_t)bound); }
};

// Latin-hypercube samples over [lb, ub]^d (one stratified cell per sample
// and dimension, like scipy qmc.LatinHypercube).
std::vector<std::vector<double>> lhsStarts(int n, const std::vector<double>& lb,
                                           const std::vector<double>& ub, Rng& rng);

}  // namespace tf
