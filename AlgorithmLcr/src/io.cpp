#include "lcr/lcr.hpp"
#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace lcr {
namespace {
struct Line {
  int n;
  std::vector<std::string> fields;
};
std::vector<Line> lines(std::istream &in) {
  std::vector<Line> out;
  std::string s;
  int n = 0;
  while (std::getline(in, s)) {
    ++n;
    s = s.substr(0, s.find('#'));
    std::istringstream ss(s);
    Line l{n, {}};
    while (ss >> s)
      l.fields.push_back(s);
    if (!l.fields.empty())
      out.push_back(l);
  }
  return out;
}
[[noreturn]] void fail(const Line &l, const std::string &why) {
  throw std::invalid_argument("line " + std::to_string(l.n) + ": " + why);
}
void fields(const Line &l, size_t n) {
  if (l.fields.size() != n)
    fail(l, "expected " + std::to_string(n) + " fields");
}
double number(const Line &l, size_t i) {
  try {
    const auto &token = l.fields.at(i);
    if (token.find_first_not_of("0123456789+-.eE") != std::string::npos)
      fail(l, "expected decimal number");
    char *end = nullptr;
    double v = std::strtod(token.c_str(), &end);
    if (end == token.c_str() || *end || !std::isfinite(v))
      fail(l, "non-finite/invalid number field " + std::to_string(i + 1));
    return v;
  } catch (const std::exception &) {
    fail(l, "invalid finite number field " + std::to_string(i + 1));
  }
}
int integer(const Line &l, size_t i, int minimum = 0) {
  auto s = l.fields.at(i);
  if (!s.empty() && s[0] == '+')
    s.erase(0, 1);
  if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
    fail(l, "invalid integer");
  try {
    auto v = std::stoll(s);
    if (v < minimum || v > 1000000)
      fail(l, "integer outside supported range");
    return int(v);
  } catch (const std::exception &) {
    fail(l, "integer outside supported range");
  }
}
char kind(const Line &l, size_t i) {
  auto s = l.fields.at(i);
  if (s != "R" && s != "L" && s != "C")
    fail(l, "type must be R/L/C");
  return s[0];
}
} // namespace
Data loadMeasurements(std::istream &in) {
  auto ls = lines(in);
  if (ls.empty())
    throw std::invalid_argument("line 1: missing measurements");
  fields(ls[0], 1);
  int n = integer(ls[0], 0, 1);
  if (ls.size() != size_t(n + 1))
    fail(ls[0], "measurement row count mismatch");
  Data d;
  for (size_t i = 1; i < ls.size(); ++i) {
    auto &l = ls[i];
    fields(l, 3);
    double f = number(l, 0);
    if (f <= 0)
      fail(l, "frequency must be positive");
    d.push_back({f, {number(l, 1), number(l, 2)}});
  }
  return d;
}
Data loadCsv(std::istream &in) {
  std::ostringstream body;
  std::string s;
  int n = 0;
  while (std::getline(in, s)) {
    s = s.substr(0, s.find('#'));
    if (s.find_first_not_of(" \t\r") == std::string::npos)
      continue;
    std::replace(s.begin(), s.end(), ',', ' ');
    body << s << '\n';
    ++n;
  }
  std::istringstream all(std::to_string(n) + "\n" + body.str());
  return loadMeasurements(all);
}
void dumpMeasurements(std::ostream &out, const Data &d) {
  out << d.size() << '\n' << std::setprecision(17);
  for (auto p : d)
    out << p.f << ' ' << p.z.real() << ' ' << p.z.imag() << '\n';
}
int loadCount(std::istream &in) {
  auto ls = lines(in);
  if (ls.size() != 1)
    throw std::invalid_argument("line 1: count requires exactly one row");
  fields(ls[0], 1);
  return integer(ls[0], 0, 1);
}
std::vector<Edge> loadComponents(std::istream &in) {
  auto ls = lines(in);
  if (ls.empty())
    throw std::invalid_argument("line 1: missing components");
  std::vector<Edge> out;
  for (auto &l : ls) {
    char t = kind(l, 0);
    if (t == 'L' && l.fields.size() == 3) {
    } else
      fields(l, 2);
    double v = number(l, 1), r = l.fields.size() == 3 ? number(l, 2) : 0;
    if (v <= 0 || r < 0)
      fail(l, "parameter must be >0 and DCR >=0");
    out.push_back({t, v, r});
  }
  std::sort(out.begin(), out.end(), [](auto a, auto b) {
    return std::tie(a.type, a.parameter, a.parameterOfCapacitanceDCResistance) <
           std::tie(b.type, b.parameter, b.parameterOfCapacitanceDCResistance);
  });
  return out;
}
Graph loadTopology(std::istream &in) {
  auto ls = lines(in);
  if (ls.empty())
    throw std::invalid_argument("line 1: missing topology");
  fields(ls[0], 1);
  Graph g;
  g.vertices = integer(ls[0], 0, 2);
  if (g.vertices > 256)
    fail(ls[0], "node limit 256");
  if (ls.size() < size_t(g.vertices))
    fail(ls[0], "missing triangle rows");
  size_t q = g.vertices;
  for (int u = 0; u < g.vertices - 1; ++u) {
    auto &l = ls[u + 1];
    fields(l, g.vertices - u - 1);
    for (int v = u + 1; v < g.vertices; ++v) {
      int n = integer(l, v - u - 1);
      if (n > 100000 || q + size_t(n) > ls.size())
        fail(l, "edge queue too short");
      for (int k = 0; k < n; ++k) {
        fields(ls[q], 1);
        g.edges.push_back({u, v, {kind(ls[q], 0), 1, 0}});
        ++q;
      }
    }
  }
  if (q != ls.size())
    fail(ls[q], "extra edge queue rows");
  return g;
}
void validate(const Data &d, const Config &c) {
  if (d.empty())
    throw std::invalid_argument("empty measurements");
  for (auto p : d)
    if (!(p.f > 0) || !std::isfinite(p.f) || !std::isfinite(p.z.real()) ||
        !std::isfinite(p.z.imag()))
      throw std::invalid_argument(
          "non-finite measurement or nonpositive frequency");
  if (c.topK < 1 || c.starts < 1 || c.iterations < 1 || c.maxN < 1 ||
      c.maxN > 12 || c.maxDepth < 1 ||
      (c.exactN && (*c.exactN < 1 || *c.exactN > 12)) ||
      !(c.relativeFloor > 0 && std::isfinite(c.relativeFloor)) ||
      !(c.equivalenceTolerance >= 0 && std::isfinite(c.equivalenceTolerance)) ||
      !(c.seconds >= 0) || !std::isfinite(c.seconds) ||
      !(c.tolerance >= 0 && c.tolerance < 1) ||
      !(c.dcrAbsoluteTolerance >= 0 && std::isfinite(c.dcrAbsoluteTolerance)))
    throw std::invalid_argument("invalid configuration");
  if (c.dcrAbsoluteTolerance > 0 && c.tolerance == 0)
    throw std::invalid_argument(
        "DCR tolerance requires explicit fractional tolerance mode");
  for (auto b : {std::pair<double, double>{c.rMin, c.rMax},
                 {c.lMin, c.lMax},
                 {c.cMin, c.cMax}})
    if (!(b.first > 0 && b.second > b.first && std::isfinite(b.second)))
      throw std::invalid_argument("invalid parameter bounds");
  if (!(c.dcrMax > 0 && std::isfinite(c.dcrMax)))
    throw std::invalid_argument("invalid DCR bound");
  if (!c.covariance.empty()) {
    if (c.covariance.size() != d.size())
      throw std::invalid_argument("covariance size mismatch");
    for (auto &m : c.covariance) {
      Eigen::LLT<Eigen::Matrix2d> llt(m);
      if (!m.allFinite() || !m.isApprox(m.transpose()) ||
          llt.info() != Eigen::Success)
        throw std::invalid_argument(
            "covariance must be symmetric positive definite");
    }
  }
}
void validate(const Graph &g) {
  if (g.vertices < 2 || g.vertices > 256)
    throw std::invalid_argument("invalid vertices");
  for (auto b : g.edges)
    if (b.u < 0 || b.v < 0 || b.u >= g.vertices || b.v >= g.vertices ||
        b.u == b.v ||
        (b.element.type != 'R' && b.element.type != 'L' &&
         b.element.type != 'C') ||
        !(b.element.parameter > 0) || !std::isfinite(b.element.parameter) ||
        !(b.element.parameterOfCapacitanceDCResistance >= 0) ||
        !std::isfinite(b.element.parameterOfCapacitanceDCResistance) ||
        (b.element.type != 'L' &&
         b.element.parameterOfCapacitanceDCResistance != 0))
      throw std::invalid_argument("invalid graph edge");
}
Upper adjacency(const Graph &g) {
  validate(g);
  Upper a(g.vertices);
  for (int i = 0; i < g.vertices; ++i)
    a[i].resize(g.vertices - i - 1);
  for (auto b : g.edges) {
    if (b.u > b.v)
      std::swap(b.u, b.v);
    a[b.u][b.v - b.u - 1].push_back(b.element);
  }
  return a;
}
void printAdjacency(std::ostream &out, const Graph &g, int rank) {
  auto a = adjacency(g);
  out << "adjacency[" << rank << "] V=" << g.vertices << " (ports 0,1):\n";
  auto flags = out.flags();
  auto precision = out.precision();
  out << std::scientific << std::setprecision(3);
  for (int u = 0; u < g.vertices; ++u)
    for (int v = u + 1; v < g.vertices; ++v) {
      auto &es = a[u][v - u - 1];
      if (es.empty())
        continue;
      out << "  (" << u << "," << v << "): ";
      for (size_t i = 0; i < es.size(); ++i) {
        if (i)
          out << " | ";
        auto e = es[i];
        out << e.type << ' ' << e.parameter;
        if (e.type == 'L' && e.parameterOfCapacitanceDCResistance != 0)
          out << " dcr " << e.parameterOfCapacitanceDCResistance;
      }
      out << '\n';
    }
  out.flags(flags);
  out.precision(precision);
}
} // namespace lcr
