#include "iofmt.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ng {

namespace {

std::vector<std::string> contentLines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream is(text);
    std::string raw;
    while (std::getline(is, raw)) {
        size_t hash = raw.find('#');
        if (hash != std::string::npos) raw = raw.substr(0, hash);
        size_t a = raw.find_first_not_of(" \t\r");
        size_t b = raw.find_last_not_of(" \t\r");
        if (a == std::string::npos) continue;
        out.push_back(raw.substr(a, b - a + 1));
    }
    return out;
}

[[noreturn]] void fail(const std::string& msg) { throw std::invalid_argument(msg); }

std::string readFile(const std::string& path) {
    std::ifstream fh(path, std::ios::binary);
    if (!fh) fail("cannot open input file: " + path);
    std::ostringstream ss;
    ss << fh.rdbuf();
    return ss.str();
}

std::vector<std::string> splitFields(const std::string& line) {
    std::istringstream ls(line);
    std::vector<std::string> parts;
    std::string tok;
    while (ls >> tok) parts.push_back(tok);
    return parts;
}

}  // namespace

Measurements parseMeasurements(const std::string& text) {
    auto rows = contentLines(text);
    if (rows.empty()) fail("empty measurement input");
    size_t n = 0;
    try {
        size_t pos = 0;
        long v = std::stol(rows[0], &pos);
        if (pos != rows[0].size()) throw std::invalid_argument("trailing");
        if (v < 1) throw std::invalid_argument("positive");
        n = (size_t)v;
    } catch (const std::invalid_argument& e) {
        if (std::string(e.what()) == "positive")
            fail("n must be a positive integer");
        if (std::string(e.what()) == "trailing")
            fail("first line must be integer n, got '" + rows[0] + "'");
        fail("first line must be integer n, got '" + rows[0] + "'");
    } catch (const std::exception&) {
        fail("first line must be integer n, got '" + rows[0] + "'");
    }
    if (rows.size() - 1 != n)
        fail("n=" + std::to_string(n) + " but " + std::to_string(rows.size() - 1) +
             " data lines follow");
    Measurements out;
    out.f.reserve(n);
    out.z.reserve(n);
    for (size_t k = 0; k < n; ++k) {
        const std::string& line = rows[k + 1];
        auto parts = splitFields(line);
        if (parts.size() != 3)
            fail("line " + std::to_string(k + 2) + ": expected 'f Rz Iz', got '" + line + "'");
        double fv, rz, iz;
        try {
            size_t p0, p1, p2;
            fv = std::stod(parts[0], &p0);
            rz = std::stod(parts[1], &p1);
            iz = std::stod(parts[2], &p2);
            if (p0 != parts[0].size() || p1 != parts[1].size() || p2 != parts[2].size())
                throw std::invalid_argument("trailing");
        } catch (const std::exception&) {
            fail("line " + std::to_string(k + 2) + ": non-numeric field in '" + line + "'");
        }
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

ComponentSet parseComponents(const std::string& text) {
    auto rows = contentLines(text);
    if (rows.empty()) fail("need at least one component line");
    std::vector<Component> comps;
    for (size_t k = 0; k < rows.size(); ++k) {
        const std::string& line = rows[k];
        auto parts = splitFields(line);
        std::string where = "line " + std::to_string(k + 1) + ": ";
        if (parts.empty()) continue;
        const std::string& kind = parts[0];
        if (kind.size() != 1 || (kind[0] != 'R' && kind[0] != 'L' && kind[0] != 'C'))
            fail(where + "type must be R|L|C, got '" + kind + "'");
        char kd = kind[0];
        if (kd != 'L' && parts.size() != 2)
            fail(where + std::string(1, kd) + " line needs exactly 'type parameter', got '" +
                 line + "'");
        if (kd == 'L' && parts.size() != 2 && parts.size() != 3)
            fail(where + "L line needs 'type parameter [dcr]', got '" + line + "'");
        double value = 0.0, dcr = 0.0;
        try {
            size_t p1 = 0;
            value = std::stod(parts[1], &p1);
            if (p1 != parts[1].size()) throw std::invalid_argument("trailing");
            if (kd == 'L' && parts.size() == 3) {
                size_t p2 = 0;
                dcr = std::stod(parts[2], &p2);
                if (p2 != parts[2].size()) throw std::invalid_argument("trailing");
            }
        } catch (const std::exception&) {
            fail(where + "non-numeric field in '" + line + "'");
        }
        try {
            comps.push_back(makeComponent(kd, value, dcr));
        } catch (const std::invalid_argument& e) {
            fail(where + e.what());
        }
    }
    return ComponentSet(std::move(comps));
}

ComponentSet loadComponents(const std::string& path) {
    return parseComponents(readFile(path));
}

std::string formatComponents(const ComponentSet& compset) {
    std::ostringstream out;
    char buf[2][32];
    for (const auto& c : compset.components()) {
        if (c.kind == 'L') {
            std::snprintf(buf[0], sizeof(buf[0]), "%.17g", c.value);
            std::snprintf(buf[1], sizeof(buf[1]), "%.17g", c.dcr);
            out << "L " << buf[0] << " " << buf[1] << "\n";
        } else {
            std::snprintf(buf[0], sizeof(buf[0]), "%.17g", c.value);
            out << c.kind << " " << buf[0] << "\n";
        }
    }
    return out.str();
}

}  // namespace ng
