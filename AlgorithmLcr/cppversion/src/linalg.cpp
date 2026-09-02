#include "linalg.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rlc {

namespace {
constexpr double kEps = std::numeric_limits<double>::epsilon();
}  // namespace

bool solveSPD(std::vector<double>& A, int n, std::vector<double>& b) {
    // Cholesky factorization A = L L^T (lower triangle overwritten).
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j <= i; ++j) {
            double sum = A[(size_t)i * n + j];
            for (int k = 0; k < j; ++k) sum -= A[(size_t)i * n + k] * A[(size_t)j * n + k];
            if (i == j) {
                if (sum <= 0.0 || !std::isfinite(sum)) return false;
                A[(size_t)i * n + j] = std::sqrt(sum);
            } else {
                A[(size_t)i * n + j] = sum / A[(size_t)j * n + j];
            }
        }
    }
    // forward / back substitution
    for (int i = 0; i < n; ++i) {
        double sum = b[i];
        for (int k = 0; k < i; ++k) sum -= A[(size_t)i * n + k] * b[k];
        b[i] = sum / A[(size_t)i * n + i];
    }
    for (int i = n - 1; i >= 0; --i) {
        double sum = b[i];
        for (int k = i + 1; k < n; ++k) sum -= A[(size_t)k * n + i] * b[k];
        b[i] = sum / A[(size_t)i * n + i];
    }
    return true;
}

std::vector<double> lstsqScaled(const std::vector<double>& A,
                                const std::vector<double>& y, int m, int n) {
    // 1) column scaling by 2-norms
    std::vector<double> scale(n, 1.0);
    for (int j = 0; j < n; ++j) {
        double sum = 0.0;
        for (int i = 0; i < m; ++i) {
            double v = A[(size_t)i * n + j];
            sum += v * v;
        }
        scale[j] = std::sqrt(sum);
        if (scale[j] == 0.0) scale[j] = 1.0;
    }
    std::vector<double> B((size_t)m * n);
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < n; ++j) B[(size_t)i * n + j] = A[(size_t)i * n + j] / scale[j];

    // 2) one-sided Jacobi SVD on the columns of B (m >= n)
    //    B sweeps to U Sigma, rotations accumulated into V.
    std::vector<double> V((size_t)n * n, 0.0);
    for (int j = 0; j < n; ++j) V[(size_t)j * n + j] = 1.0;

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
                double app = columnDot(p, p);
                double aqq = columnDot(q, q);
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
                for (int i = 0; i < n; ++i) {
                    double vp = V[(size_t)i * n + p], vq = V[(size_t)i * n + q];
                    V[(size_t)i * n + p] = c * vp - s * vq;
                    V[(size_t)i * n + q] = s * vp + c * vq;
                }
            }
        }
        if (!rotated) break;
    }

    // singular values from final column norms; numpy's rcond=None cutoff is
    // eps * max(m, n) * sigma_max
    std::vector<double> sigma(n);
    double smax = 0.0;
    for (int j = 0; j < n; ++j) {
        sigma[j] = std::sqrt(columnDot(j, j));
        smax = std::max(smax, sigma[j]);
    }
    double cutoff = kEps * std::max(m, n) * smax;

    // x = sum_j (u_j . y)/sigma_j * v_j  over sigma_j > cutoff
    std::vector<double> x(n, 0.0);
    for (int j = 0; j < n; ++j) {
        if (sigma[j] <= cutoff || sigma[j] == 0.0) continue;
        double uy = 0.0;
        for (int i = 0; i < m; ++i) uy += B[(size_t)i * n + j] * y[i];
        double coef = uy / sigma[j] / sigma[j];
        for (int i = 0; i < n; ++i) x[i] += coef * V[(size_t)i * n + j];
    }
    for (int j = 0; j < n; ++j) x[j] /= scale[j];
    return x;
}

std::vector<std::complex<double>> polyFromRoots(
    const std::vector<std::complex<double>>& roots) {
    // numpy.poly: highest-first coefficients of prod (x - r_i)
    std::vector<std::complex<double>> c{1.0};
    c.reserve(roots.size() + 1);
    for (const auto& r : roots) {
        // multiply by (x - r)
        std::vector<std::complex<double>> nc(c.size() + 1, {0.0, 0.0});
        for (size_t i = 0; i < c.size(); ++i) {
            nc[i] += c[i];
            nc[i + 1] -= r * c[i];
        }
        c = std::move(nc);
    }
    return c;
}

std::vector<std::complex<double>> polyRoots(const std::vector<double>& coeffs) {
    using C = std::complex<double>;
    // strip leading zeros (numpy.roots behaviour)
    std::vector<double> a = coeffs;
    size_t lead = 0;
    while (lead < a.size() && a[lead] == 0.0) ++lead;
    a.erase(a.begin(), a.begin() + (long)lead);
    if (a.size() <= 1) return {};
    // normalize to monic
    for (auto& v : a) v /= a[0];
    int n = (int)a.size() - 1;

    if (n == 1) return {C(-a[1], 0.0)};
    if (n == 2) {
        // monic x^2 + a[1] x + a[2]; roots = (-a[1] +/- sqrt(disc)) / 2
        C nb(-a[1], 0.0);
        C disc = C(a[1] * a[1] - 4.0 * a[2], 0.0);
        C sq = std::sqrt(disc);
        return {(nb + sq) / 2.0, (nb - sq) / 2.0};
    }

    // Companion-matrix eigenvalues via shifted Hessenberg QR (the numpy.roots
    // route).  Durand-Kerner proved fragile for sigma polynomials whose
    // coefficients span 8+ decades.
    // companion (already upper Hessenberg): first row -a[1..n], unit subdiag
    std::vector<std::vector<C>> H(n, std::vector<C>(n, C(0.0, 0.0)));
    for (int j = 0; j < n; ++j) H[0][j] = C(-a[j + 1], 0.0);
    for (int i = 1; i < n; ++i) H[i][i - 1] = C(1.0, 0.0);

    std::vector<C> ev;
    ev.reserve(n);
    int h = n - 1;
    int stuck = 0;
    int budget = 400 * n;  // hard cap against non-deflating edge cases
    const double tol = 30.0 * kEps;
    while (h >= 0) {
        if (--budget < 0) {  // force-deflate the rest (never hang)
            while (h >= 0) {
                ev.push_back(H[h][h]);
                --h;
            }
            break;
        }
        // deflation scan: find the smallest l such that block l..h is active
        int l = h;
        while (l > 0) {
            double s = std::abs(H[l - 1][l - 1]) + std::abs(H[l][l]);
            if (std::abs(H[l][l - 1]) <= tol * std::max(s, 1e-300)) {
                H[l][l - 1] = C(0.0, 0.0);
                break;
            }
            --l;
        }
        if (l == h) {
            ev.push_back(H[h][h]);
            --h;
            stuck = 0;
            continue;
        }
        if (l == h - 1) {
            C aa = H[l][l], bb = H[l][h], cc = H[h][l], dd = H[h][h];
            C tr = aa + dd, det = aa * dd - bb * cc;
            C disc = std::sqrt(tr * tr - 4.0 * det);
            ev.push_back((tr + disc) / 2.0);
            ev.push_back((tr - disc) / 2.0);
            h -= 2;
            stuck = 0;
            continue;
        }
        // Wilkinson shift from the trailing 2x2 of the active block; an
        // ad-hoc exceptional shift breaks rare stagnation cycles
        C aa = H[h - 1][h - 1], cc = H[h][h - 1], dd = H[h][h];
        C mu;
        if (stuck > 0 && stuck % 25 == 0) {
            mu = dd + C(0.75 * std::abs(cc), 0.0);
        } else {
            C delta = (aa - dd) / 2.0;
            C den = delta + std::sqrt(delta * delta + cc * cc);
            mu = (den == C(0.0, 0.0)) ? dd : dd - cc * cc / den;
        }
        // explicit shifted QR sweep: Q R = H - mu I, H' = R Q + mu I.
        // Pass 1: row rotations build R; pass 2: column rotations form R Q.
        for (int i = l; i <= h; ++i) H[i][i] -= mu;
        std::vector<C> cs(h - l + 1), ss(h - l + 1);
        for (int k = l; k <= h - 1; ++k) {
            C x = H[k][k], y = H[k + 1][k];
            double nr = std::sqrt(std::norm(x) + std::norm(y));
            if (nr == 0.0) {
                cs[k - l] = C(1.0, 0.0);
                ss[k - l] = C(0.0, 0.0);
                continue;
            }
            cs[k - l] = x / nr;
            ss[k - l] = y / nr;
            // G^H rows k,k+1 over cols k..h
            C c = cs[k - l], s = ss[k - l];
            for (int j = k; j <= h; ++j) {
                C h1 = std::conj(c) * H[k][j] + std::conj(s) * H[k + 1][j];
                C h2 = -s * H[k][j] + c * H[k + 1][j];
                H[k][j] = h1;
                H[k + 1][j] = h2;
            }
        }
        for (int k = l; k <= h - 1; ++k) {
            C c = cs[k - l], s = ss[k - l];
            // right-multiply by G^H: same 2x2 action as the row pass
            // col_k' = conj(c) col_k + conj(s) col_{k+1}
            // col_{k+1}' = -s col_k + c col_{k+1}
            int rmax = std::min(k + 2, h);
            for (int i = l; i <= rmax; ++i) {
                C h1 = std::conj(c) * H[i][k] + std::conj(s) * H[i][k + 1];
                C h2 = -s * H[i][k] + c * H[i][k + 1];
                H[i][k] = h1;
                H[i][k + 1] = h2;
            }
        }
        for (int i = l; i <= h; ++i) H[i][i] += mu;
        ++stuck;
    }

    // Newton polish on the ORIGINAL monic polynomial
    for (auto& z : ev) {
        for (int k = 0; k < 4; ++k) {
            C p{0.0, 0.0}, d{0.0, 0.0};
            for (double v : a) {
                d = d * z + p;
                p = p * z + C(v, 0.0);
            }
            if (std::abs(d) < 1e-300) break;
            C step = p / d;
            if (!std::isfinite(step.real()) || !std::isfinite(step.imag())) break;
            z -= step;
            if (std::abs(step) < 1e-16 * std::max(1.0, std::abs(z))) break;
        }
    }
    // clean tiny imaginary parts introduced by rounding
    for (auto& r : ev) {
        if (std::fabs(r.imag()) < 1e-12 * std::max(1.0, std::fabs(r.real())))
            r = C(r.real(), 0.0);
    }
    return ev;
}

double polyfitSlope(const double* x, const double* y, int k) {
    double mx = 0.0, my = 0.0;
    for (int i = 0; i < k; ++i) {
        mx += x[i];
        my += y[i];
    }
    mx /= k;
    my /= k;
    double num = 0.0, den = 0.0;
    for (int i = 0; i < k; ++i) {
        num += (x[i] - mx) * (y[i] - my);
        den += (x[i] - mx) * (x[i] - mx);
    }
    if (den == 0.0) return 0.0;
    return num / den;
}

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if (n % 2 == 1) return v[n / 2];
    return 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

std::vector<double> geomspace(double a, double b, int n) {
    std::vector<double> out(n);
    if (n == 1) {
        out[0] = a;
        return out;
    }
    double la = std::log10(a), lb = std::log10(b);
    for (int i = 0; i < n; ++i) {
        if (i == 0) {
            out[i] = a;
        } else if (i == n - 1) {
            out[i] = b;
        } else {
            out[i] = std::pow(10.0, la + (lb - la) * (double)i / (double)(n - 1));
        }
    }
    return out;
}

void Rng::shuffle(std::vector<int>& v) {
    for (size_t i = v.size(); i > 1; --i) {
        size_t j = (size_t)intBelow((uint64_t)i);
        std::swap(v[i - 1], v[j]);
    }
}

std::vector<std::vector<double>> lhsStarts(int n, const std::vector<double>& lb,
                                           const std::vector<double>& ub, Rng& rng) {
    // scrambled Latin hypercube: per dimension one stratified sample per cell
    if (n <= 0) return {};
    const size_t d = lb.size();
    std::vector<std::vector<double>> out(n, std::vector<double>(d));
    std::vector<int> perm(n);
    for (size_t dim = 0; dim < d; ++dim) {
        for (int i = 0; i < n; ++i) perm[i] = i;
        rng.shuffle(perm);
        for (int i = 0; i < n; ++i) {
            double u = (perm[i] + rng.uniform01()) / (double)n;
            out[i][dim] = lb[dim] + u * (ub[dim] - lb[dim]);
        }
    }
    return out;
}

}  // namespace rlc
