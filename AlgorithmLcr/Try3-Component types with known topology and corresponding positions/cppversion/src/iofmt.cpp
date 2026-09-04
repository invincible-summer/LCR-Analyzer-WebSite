#include "iofmt.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace tf {

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

std::vector<std::pair<int, int>> slotPairs(int V) {
    std::vector<std::pair<int, int>> out;
    for (int i = 0; i < V; ++i)
        for (int j = i + 1; j < V; ++j) out.push_back({i, j});
    return out;
}

}  // namespace

Measurements parseMeasurements(const std::string& text) {
    auto rows = contentLines(text);
    if (rows.empty()) fail("empty measurement input");
    long n = 0;
    try {
        size_t pos = 0;
        n = std::stol(rows[0], &pos);
        if (pos != rows[0].size()) throw std::invalid_argument("trailing");
    } catch (const std::exception&) {
        fail("first line must be integer n, got '" + rows[0] + "'");
    }
    if (n < 1) fail("n must be a positive integer");
    if ((long)rows.size() - 1 != n)
        fail("n=" + std::to_string(n) + " but " + std::to_string(rows.size() - 1) +
             " data lines follow");
    Measurements out;
    out.f.reserve((size_t)n);
    out.z.reserve((size_t)n);
    for (long k = 0; k < n; ++k) {
        const std::string& line = rows[(size_t)k + 1];
        auto parts = splitFields(line);
        if (parts.size() != 3)
            fail("line " + std::to_string(k + 2) + ": expected 'f Rz Iz', got '" + line + "'");
        double fv = 0, rz = 0, iz = 0;
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
                               const std::vector<Complex>& z) {
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

std::vector<std::tuple<int, int, char>> parseTopology(const std::string& text) {
    auto rows = contentLines(text);
    if (rows.empty()) fail("empty topology input");
    int V = 0;
    try {
        size_t pos = 0;
        long v = std::stol(rows[0], &pos);
        if (pos != rows[0].size()) throw std::invalid_argument("trailing");
        V = (int)v;
    } catch (const std::exception&) {
        fail("first line must be integer V, got '" + rows[0] + "'");
    }
    if (V < 2) fail("V must be >= 2");
    if ((int)rows.size() < V)
        fail("expected V-1=" + std::to_string(V - 1) + " matrix rows, got " +
             std::to_string((int)rows.size() - 1) + " lines after V");
    std::vector<int> counts;
    for (int i = 0; i < V - 1; ++i) {
        const std::string& line = rows[(size_t)(1 + i)];
        auto parts = splitFields(line);
        if ((int)parts.size() != V - 1 - i)
            fail("matrix row " + std::to_string(i) + " needs " +
                 std::to_string(V - 1 - i) + " integers, got '" + line + "'");
        for (const auto& p : parts) {
            long m = 0;
            try {
                size_t pos = 0;
                m = std::stol(p, &pos);
                if (pos != p.size()) throw std::invalid_argument("trailing");
            } catch (const std::exception&) {
                fail("matrix row " + std::to_string(i) + ": non-integer count '" + p + "'");
            }
            if (m < 0)
                fail("matrix row " + std::to_string(i) + ": negative count " +
                     std::to_string(m));
            counts.push_back((int)m);
        }
    }
    long E = 0;
    for (int m : counts) E += m;
    long queueLen = (long)rows.size() - 1 - (V - 1);
    if (queueLen != E)
        fail("edge queue needs exactly E=" + std::to_string(E) + " lines, got " +
             std::to_string(queueLen));
    std::vector<std::tuple<int, int, char>> edges;
    auto slots = slotPairs(V);
    size_t t = 0;
    for (size_t s = 0; s < slots.size(); ++s) {
        for (int q = 0; q < counts[s]; ++q) {
            const std::string& kind = rows[1 + (size_t)(V - 1) + t];
            ++t;
            if (kind.size() != 1 || (kind[0] != 'R' && kind[0] != 'L' && kind[0] != 'C'))
                fail("edge queue entry must be R|L|C, got '" + kind + "'");
            edges.push_back({slots[s].first, slots[s].second, kind[0]});
        }
    }
    return edges;
}

std::vector<std::tuple<int, int, char>> loadTopology(const std::string& path) {
    return parseTopology(readFile(path));
}

std::string formatTopology(const std::vector<std::tuple<int, int, char>>& edges) {
    if (edges.empty()) fail("need at least one edge");
    int V = 0;
    for (const auto& [u, v, k] : edges) {
        (void)k;
        V = std::max({V, u + 1, v + 1});
    }
    std::vector<std::tuple<int, int, char>> ordered = edges;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto& a, const auto& b) {
                         int ua = std::min(std::get<0>(a), std::get<1>(a));
                         int va = std::max(std::get<0>(a), std::get<1>(a));
                         int ub = std::min(std::get<0>(b), std::get<1>(b));
                         int vb = std::max(std::get<0>(b), std::get<1>(b));
                         return std::make_pair(ua, va) < std::make_pair(ub, vb);
                     });
    std::map<std::pair<int, int>, int> counts;
    for (const auto& [u, v, k] : ordered) {
        (void)k;
        counts[{std::min(u, v), std::max(u, v)}] += 1;
    }
    std::ostringstream out;
    out << V << "\n";
    for (int i = 0; i < V - 1; ++i) {
        for (int j = i + 1; j < V; ++j) {
            if (j > i + 1) out << " ";
            auto it = counts.find({i, j});
            out << (it == counts.end() ? 0 : it->second);
        }
        out << "\n";
    }
    for (const auto& [u, v, k] : ordered) {
        (void)u;
        (void)v;
        out << k << "\n";
    }
    return out.str();
}

}  // namespace tf
