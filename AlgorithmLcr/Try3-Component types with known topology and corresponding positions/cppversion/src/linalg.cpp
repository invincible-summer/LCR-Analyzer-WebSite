#include "linalg.hpp"

#include <algorithm>
#include <cmath>

namespace tf {

bool luSolveComplex(std::vector<Complex>& A, int n, std::vector<Complex>& b) {
    for (int col = 0; col < n; ++col) {
        int piv = col;
        double best = std::abs(A[(size_t)col * n + col]);
        for (int r = col + 1; r < n; ++r) {
            double a = std::abs(A[(size_t)r * n + col]);
            if (a > best) {
                best = a;
                piv = r;
            }
        }
        if (!(best > 0.0) || !std::isfinite(best)) return false;
        if (piv != col) {
            for (int j = 0; j < n; ++j)
                std::swap(A[(size_t)piv * n + j], A[(size_t)col * n + j]);
            std::swap(b[piv], b[col]);
        }
        Complex d = A[(size_t)col * n + col];
        for (int r = col + 1; r < n; ++r) {
            Complex f = A[(size_t)r * n + col] / d;
            if (f == Complex(0.0, 0.0)) continue;
            A[(size_t)r * n + col] = f;
            for (int j = col + 1; j < n; ++j)
                A[(size_t)r * n + j] -= f * A[(size_t)col * n + j];
            b[r] -= f * b[col];
        }
    }
    for (int i = n - 1; i >= 0; --i) {
        Complex s = b[i];
        for (int j = i + 1; j < n; ++j) s -= A[(size_t)i * n + j] * b[j];
        b[i] = s / A[(size_t)i * n + i];
        if (!std::isfinite(b[i].real()) || !std::isfinite(b[i].imag())) return false;
    }
    return true;
}

std::vector<double> svdValues(const std::vector<double>& A, int m, int n) {
    // one-sided Jacobi on the columns (B sweeps to U Sigma, orthogonal
    // columns), singular values = final column norms.
    std::vector<double> B = A;
    auto columnDot = [&](int a, int b) {
        double s = 0.0;
        for (int i = 0; i < m; ++i) s += B[(size_t)i * n + a] * B[(size_t)i * n + b];
        return s;
    };
    const int maxSweeps = 60;
    for (int sweep = 0; sweep < maxSweeps; ++sweep) {
        bool rotated = false;
        for (int p = 0; p < n - 1; ++p) {
            for (int q = p + 1; q < n; ++q) {
                double app = columnDot(p, p), aqq = columnDot(q, q);
                double apq = columnDot(p, q);
                if (std::fabs(apq) <= 1e-15 * std::sqrt(app * aqq + 1e-300)) continue;
                rotated = true;
                double tau = (aqq - app) / (2.0 * apq);
                double t = (tau >= 0.0 ? 1.0 : -1.0) /
                           (std::fabs(tau) + std::sqrt(1.0 + tau * tau));
                double c = 1.0 / std::sqrt(1.0 + t * t);
                double s = c * t;
                for (int i = 0; i < m; ++i) {
                    double bp = B[(size_t)i * n + p], bq = B[(size_t)i * n + q];
                    B[(size_t)i * n + p] = c * bp - s * bq;
                    B[(size_t)i * n + q] = s * bp + c * bq;
                }
            }
        }
        if (!rotated) break;
    }
    std::vector<double> sv(n);
    for (int j = 0; j < n; ++j) sv[j] = std::sqrt(std::max(0.0, columnDot(j, j)));
    std::sort(sv.begin(), sv.end(), std::greater<double>());
    return sv;
}

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if (n % 2 == 1) return v[n / 2];
    return 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

std::vector<std::vector<double>> lhsStarts(int n, const std::vector<double>& lb,
                                           const std::vector<double>& ub, Rng& rng) {
    if (n <= 0) return {};
    const size_t d = lb.size();
    std::vector<std::vector<double>> out(n, std::vector<double>(d));
    std::vector<int> perm((size_t)n);
    for (size_t dim = 0; dim < d; ++dim) {
        for (int i = 0; i < n; ++i) perm[i] = i;
        for (size_t i = (size_t)n; i > 1; --i) {
            size_t j = (size_t)(rng.eng() % (uint64_t)i);
            std::swap(perm[i - 1], perm[j]);
        }
        for (int i = 0; i < n; ++i) {
            double u = (perm[i] + rng.uniform01()) / (double)n;
            out[i][dim] = lb[dim] + u * (ub[dim] - lb[dim]);
        }
    }
    return out;
}

}  // namespace tf
