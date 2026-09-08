#include "lcr/lcr.hpp"
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>
namespace {
char *copy(const std::string &s) {
  auto p = static_cast<char *>(std::malloc(s.size() + 1));
  if (p)
    std::memcpy(p, s.c_str(), s.size() + 1);
  return p;
}
template <class F> char *guard(F f) {
  try {
    return copy(f());
  } catch (const std::invalid_argument &e) {
    std::ostringstream o;
    o << "{\"ok\":false,\"code\":\"bad_input\",\"error\":\"";
    for (char c : std::string(e.what()))
      o << (c == '"' || c == '\\' || c < ' ' ? ' ' : c);
    o << "\"}";
    return copy(o.str());
  } catch (const std::runtime_error &e) {
    if (std::string(e.what()) == "PORT_OPEN")
      return copy("{\"ok\":false,\"code\":\"port_open\",\"error\":\"No path "
                  "between terminals 0 and 1\"}");
    return copy("{\"ok\":false,\"code\":\"internal\",\"error\":\"C++ engine "
                "failure\"}");
  } catch (...) {
    return copy("{\"ok\":false,\"code\":\"internal\",\"error\":\"C++ engine "
                "failure\"}");
  }
}
lcr::Data data(const double *f, const double *re, const double *im, int n) {
  if (n < 4 || n > 100000 || !f || !re || !im)
    throw std::invalid_argument("measurement count must be 4..100000");
  lcr::Data d;
  for (int i = 0; i < n; ++i)
    d.push_back({f[i], {re[i], im[i]}});
  return d;
}
lcr::Config config;
std::string output(const lcr::SearchResult &r, const lcr::Data &d,
                   const lcr::Config &c) {
  std::ostringstream o;
  lcr::report(o, r, d, c, true);
  return o.str();
}
} // namespace
extern "C" {
void lcr_free(char *p) { std::free(p); }
const char *lcr_version() {
  return "lcr.native.v4 revision 2 / wasm 4.1.0";
}
// Reset + common options. Extended settings go through the dedicated
// setters below; nothing in the binding layer silently rewrites other
// configuration fields (maxDepth in particular follows maxN nowhere).
void lcr_configure(int fast, int budget, double seconds, double tolerance,
                   double dcr, int robust) {
  config = lcr::Config{};
  config.mode = fast ? lcr::Config::Fast : lcr::Config::Strict;
  config.candidateBudget = budget > 0 ? budget : 0;
  config.seconds = seconds;
  config.tolerance = tolerance;
  config.dcrAbsoluteTolerance = dcr;
  config.robust = robust != 0;
}
void lcr_configure_search(int fast, int budget, double seconds, int starts,
                          int iterations, unsigned seed,
                          double equivalenceTolerance) {
  config.mode = fast ? lcr::Config::Fast : lcr::Config::Strict;
  config.candidateBudget = budget > 0 ? budget : 0;
  config.seconds = seconds;
  config.starts = starts;
  config.iterations = iterations;
  config.seed = seed;
  config.equivalenceTolerance = equivalenceTolerance;
}
void lcr_configure_bounds(double rMin, double rMax, double lMin, double lMax,
                          double cMin, double cMax, double dcrMax,
                          double relativeFloor) {
  config.rMin = rMin;
  config.rMax = rMax;
  config.lMin = lMin;
  config.lMax = lMax;
  config.cMin = cMin;
  config.cMax = cMax;
  config.dcrMax = dcrMax;
  config.relativeFloor = relativeFloor;
}
// All-or-nothing per-point Cartesian covariance; null clears it back to the
// relative fallback. SPD is authoritatively validated with the data.
void lcr_set_covariance(const double *rr, const double *ri, const double *ii,
                        int n) {
  if (!rr || !ri || !ii || n <= 0) {
    config.covariance.clear();
    return;
  }
  config.covariance.assign(n, Eigen::Matrix2d{});
  for (int i = 0; i < n; ++i) {
    Eigen::Matrix2d m;
    m << rr[i], ri[i], ri[i], ii[i];
    config.covariance[i] = m;
  }
}
char *lcr_try1(const double *f, const double *re, const double *im, int n,
               int exactN, int maxN, int maxDepth, int topK) {
  return guard([&] {
    auto d = data(f, re, im, n);
    auto c = config;
    c.topK = topK;
    if (exactN < 0 || exactN > 12 || maxN < 0 || maxN > 12 ||
        maxDepth < 0 || maxDepth > 12)
      throw std::invalid_argument("device count must be 1..12");
    if (exactN)
      c.exactN = exactN;
    if (maxN)
      c.maxN = maxN;
    else if (exactN)
      c.maxN = exactN;
    if (maxDepth)
      c.maxDepth = maxDepth;
    return output(lcr::try1(d, c), d, c);
  });
}
char *lcr_try2(const double *f, const double *re, const double *im, int n,
               const int *kinds, const double *values, const double *dcrs,
               const int *counts, int rows, int topK) {
  return guard([&] {
    auto d = data(f, re, im, n);
    auto c = config;
    c.topK = topK;
    if (rows < 1 || rows > 8 || !kinds || !values || !dcrs || !counts)
      throw std::invalid_argument("invalid component rows");
    std::vector<lcr::Edge> es;
    for (int i = 0; i < rows; ++i) {
      if (counts[i] < 1 || counts[i] > 8 || es.size() + counts[i] > 8)
        throw std::invalid_argument("Try2 supports 1..8 devices");
      for (int j = 0; j < counts[i]; ++j)
        es.push_back({char(kinds[i]), values[i], dcrs[i]});
    }
    return output(lcr::try2(d, es, c), d, c);
  });
}
char *lcr_try3(const double *f, const double *re, const double *im, int n,
               const int *us, const int *vs, const int *kinds, int m) {
  return guard([&] {
    auto d = data(f, re, im, n);
    auto c = config;
    lcr::Graph g;
    if (m < 1 || m > 32 || !us || !vs || !kinds)
      throw std::invalid_argument("Try3 supports 1..32 edges");
    for (int i = 0; i < m; ++i) {
      if (us[i] < 0 || vs[i] < 0 || us[i] >= 16 || vs[i] >= 16)
        throw std::invalid_argument("node labels must be 0..15");
      g.vertices = std::max(g.vertices, std::max(us[i], vs[i]) + 1);
      char k = char(kinds[i]);
      g.edges.push_back({us[i],
                         vs[i],
                         {k,
                          k == 'C'   ? 1e-7
                          : k == 'L' ? 1e-3
                                     : 1000,
                          0}});
    }
    lcr::validate(g);
    return output(lcr::try3(d, g, c), d, c);
  });
}
}
