// common_glue.hpp — shared helpers for the three per-Try glue translation
// units.  Header-only; each try*_glue.cpp includes exactly ONE engine library
// (their headers share basenames like adjacency.hpp, so they must never meet
// in the same TU).
//
// Output contract (mirrored by frontend/src/lib/fitTypes.ts):
//   success: {"ok":true,"try":N,"elapsed":s,"candidates":[...],"tryN":{...}}
//   failure: {"ok":false,"code":"bad_input|port_open|internal","error":"..."}

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace lcr_glue {

// ---------------------------------------------------------------------------
// Minimal JSON writer (%.17g round-trip; non-finite -> 1e999 sentinel)
// ---------------------------------------------------------------------------
struct Json {
    std::string s;

    void raw(const char* p) { s += p; }
    void raw(const std::string& p) { s += p; }

    void str(const std::string& v) {
        s += '"';
        for (char c : v) {
            switch (c) {
                case '"': s += "\\\""; break;
                case '\\': s += "\\\\"; break;
                case '\n': s += "\\n"; break;
                case '\r': s += "\\r"; break;
                case '\t': s += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char b[8];
                        std::snprintf(b, sizeof(b), "\\u%04x", c);
                        s += b;
                    } else {
                        s += c;
                    }
            }
        }
        s += '"';
    }

    void num(double v) {
        if (!std::isfinite(v)) {
            s += (v > 0 || !(v < 0)) ? "1e999" : "-1e999";
            return;
        }
        char b[48];
        std::snprintf(b, sizeof(b), "%.17g", v);
        s += b;
    }

    void integer(long long v) {
        char b[24];
        std::snprintf(b, sizeof(b), "%lld", v);
        s += b;
    }

    void boolean(bool v) { s += v ? "true" : "false"; }
    void key(const char* k) { str(k); s += ':'; }
};

inline char* takeString(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (!out) std::abort();
    std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

inline char* errorOut(const char* code, const std::string& msg) {
    Json j;
    j.raw("{\"ok\":false,\"code\":");
    j.str(code);
    j.raw(",\"error\":");
    j.str(msg);
    j.raw("}");
    return takeString(j.s);
}

// ---------------------------------------------------------------------------
// Shared measurement validation: n >= 4, f > 0, everything finite.
// Returns "" when valid, else a human-readable problem description
// (1-based point index).
// ---------------------------------------------------------------------------
inline std::string checkMeasurements(const double* f, const double* zre,
                                     const double* zim, int n) {
    if (n < 4) return "measurement points must be >= 4";
    if (!f || !zre || !zim) return "null measurement arrays";
    for (int i = 0; i < n; ++i) {
        if (!(f[i] > 0.0) || !std::isfinite(f[i]))
            return "point " + std::to_string(i + 1) + ": frequency must be > 0 and finite";
        if (!std::isfinite(zre[i]) || !std::isfinite(zim[i]))
            return "point " + std::to_string(i + 1) + ": Re(Z)/Im(Z) must be finite";
    }
    return "";
}

// Display grid for theory curves: the measurement frequencies themselves plus
// `nGrid` log-spaced points over [fmin/2, fmax*2] (sorted ascending).
inline std::vector<double> theoryGrid(const std::vector<double>& f, int nGrid = 100) {
    double fmin = f.front(), fmax = f.front();
    for (double v : f) {
        fmin = std::min(fmin, v);
        fmax = std::max(fmax, v);
    }
    std::vector<double> grid = f;
    const double lo = std::log10(fmin / 2.0);
    const double hi = std::log10(fmax * 2.0);
    for (int i = 0; i < nGrid; ++i) {
        const double t = lo + (hi - lo) * static_cast<double>(i) / (nGrid - 1);
        grid.push_back(std::pow(10.0, t));
    }
    std::sort(grid.begin(), grid.end());
    return grid;
}

template <typename ComplexT>
inline void emitComplexArrays(lcr_glue::Json& j, const std::vector<double>& freqs,
                              const std::vector<ComplexT>& z) {
    j.key("theory");
    j.raw("{\"f\":[");
    for (size_t i = 0; i < freqs.size(); ++i) {
        if (i) j.raw(",");
        j.num(freqs[i]);
    }
    j.raw("],\"re\":[");
    for (size_t i = 0; i < z.size(); ++i) {
        if (i) j.raw(",");
        j.num(z[i].real());
    }
    j.raw("],\"im\":[");
    for (size_t i = 0; i < z.size(); ++i) {
        if (i) j.raw(",");
        j.num(z[i].imag());
    }
    j.raw("]}");
}

inline int clampTopK(int topK) {
    if (topK < 1) return 5;
    return std::min(topK, 20);
}

}  // namespace lcr_glue
