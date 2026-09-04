#include "iofmt.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace rlc {

namespace {

// Strip comments and blank lines (INPUT_FORMAT.md sec.0).
std::vector<std::string> contentLines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream is(text);
    std::string raw;
    while (std::getline(is, raw)) {
        size_t hash = raw.find('#');
        if (hash != std::string::npos) raw = raw.substr(0, hash);
        // trim
        size_t a = raw.find_first_not_of(" \t\r");
        size_t b = raw.find_last_not_of(" \t\r");
        if (a == std::string::npos) continue;
        out.push_back(raw.substr(a, b - a + 1));
    }
    return out;
}

[[noreturn]] void fail(const std::string& msg) {
    throw std::invalid_argument(msg);
}

std::string readFile(const std::string& path) {
    std::ifstream fh(path, std::ios::binary);
    if (!fh) fail("cannot open input file: " + path);
    std::ostringstream ss;
    ss << fh.rdbuf();
    return ss.str();
}

}  // namespace

Measurements parseMeasurements(const std::string& text) {
    auto rows = contentLines(text);
    if (rows.empty()) fail("empty measurement input");
    // first line: positive integer n
    size_t n = 0;
    try {
        n = (size_t)std::stoll(rows[0]);
    } catch (const std::exception&) {
        fail("first line must be integer n, got '" + rows[0] + "'");
    }
    if (n < 1) fail("n must be a positive integer");
    if (rows.size() - 1 != n)
        fail("n=" + std::to_string(n) + " but " +
             std::to_string(rows.size() - 1) + " data lines follow");

    Measurements out;
    out.f.reserve(n);
    out.z.reserve(n);
    for (size_t k = 0; k < n; ++k) {
        const std::string& line = rows[k + 1];
        std::istringstream ls(line);
        double fv = 0.0, rz = 0.0, iz = 0.0;
        std::string extra;
        if (!(ls >> fv >> rz >> iz)) fail("line " + std::to_string(k + 2) +
                                          ": expected 'f Rz Iz', got '" + line + "'");
        if (ls >> extra) fail("line " + std::to_string(k + 2) +
                              ": expected 3 fields, got '" + line + "'");
        if (!std::isfinite(fv) || !std::isfinite(rz) || !std::isfinite(iz))
            fail("line " + std::to_string(k + 2) + ": non-finite value");
        if (fv <= 0.0)
            fail("line " + std::to_string(k + 2) + ": frequency must be > 0");
        out.f.push_back(fv);
        out.z.emplace_back(rz, iz);
    }
    return out;
}

Measurements loadMeasurements(const std::string& path) {
    return parseMeasurements(readFile(path));
}

std::string formatMeasurements(const std::vector<double>& f,
                               const std::vector<std::complex<double>>& z) {
    if (f.size() != z.size())
        fail("f has " + std::to_string(f.size()) + " points, z has " +
             std::to_string(z.size()));
    if (f.empty()) fail("need at least one measurement point");
    std::ostringstream out;
    char buf[3][32];
    out << f.size() << "\n";
    for (size_t k = 0; k < f.size(); ++k) {
        if (!(std::isfinite(f[k]) && f[k] > 0.0))
            fail("frequency must be positive and finite");
        if (!std::isfinite(z[k].real()) || !std::isfinite(z[k].imag()))
            fail("impedance parts must be finite");
        std::snprintf(buf[0], sizeof(buf[0]), "%.17g", f[k]);
        std::snprintf(buf[1], sizeof(buf[1]), "%.17g", z[k].real());
        std::snprintf(buf[2], sizeof(buf[2]), "%.17g", z[k].imag());
        out << buf[0] << " " << buf[1] << " " << buf[2] << "\n";
    }
    return out.str();
}

int parseCount(const std::string& text) {
    auto rows = contentLines(text);
    if (rows.size() != 1)
        fail("count input must be exactly 1 line, got " +
             std::to_string(rows.size()));
    int n = 0;
    try {
        size_t pos = 0;
        long v = std::stol(rows[0], &pos);
        if (pos != rows[0].size()) throw std::invalid_argument("trailing");
        n = (int)v;
    } catch (const std::exception&) {
        fail("count line must be an integer, got '" + rows[0] + "'");
    }
    if (n < 1) fail("device count must be a positive integer");
    return n;
}

int loadCount(const std::string& path) { return parseCount(readFile(path)); }

std::string formatCount(int n) {
    if (n < 1) fail("device count must be a positive integer");
    return std::to_string(n) + "\n";
}

}  // namespace rlc
