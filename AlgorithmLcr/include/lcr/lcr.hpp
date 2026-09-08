#pragma once
#include <Eigen/Dense>
#include <array>
#include <complex>
#include <functional>
#include <iosfwd>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace lcr {
using Complex = std::complex<double>;
constexpr double pi = 3.14159265358979323846;
constexpr double inf = std::numeric_limits<double>::infinity();
struct Edge {
  char type = 'R';
  double parameter = 1;
  double parameterOfCapacitanceDCResistance = 0;
};
struct Branch {
  int u = 0, v = 1;
  Edge element;
};
struct Graph {
  int vertices = 2;
  std::vector<Branch> edges;
};
using Upper = std::vector<std::vector<std::vector<Edge>>>;
struct Point {
  double f;
  Complex z;
};
using Data = std::vector<Point>;
enum class ParamQuantity { Value, Dcr };
struct Interval {
  double lo = 0;
  double hi = 0;
};
struct EdgeDomain {
  Interval value;
  std::optional<Interval> dcr; // only L
};
enum class ExprOp {
  PrimitiveValue,
  PrimitiveDcr,
  Sum,
  HarmonicSum
};
struct ReductionExpr {
  ExprOp op = ExprOp::PrimitiveValue;
  int sourceEdge = -1;                 // primitive leaf only
  std::vector<ReductionExpr> children; // Sum/HarmonicSum
};
struct Group {
  std::vector<int> members;
  ReductionExpr valueExpr;
  std::optional<ReductionExpr> dcrExpr; // only L groups
  std::string mode = "single"; // derived summary of the last merge op
};
struct Reduction {
  Graph graph;
  std::vector<Group> groups;       // aligned 1:1 with graph.edges
  std::vector<EdgeDomain> domains; // aligned 1:1 with graph.edges
  std::vector<int> dropped;
};
struct Config {
  enum Mode { Strict, Fast } mode = Strict;
  int maxN = 4, maxDepth = 4, topK = 8, starts = 16, iterations = 160;
  std::optional<int> exactN;
  unsigned seed = 1;
  // Zero means unlimited. Fast supplies a 1000-candidate budget if unspecified.
  size_t candidateBudget = 0;
  double seconds = 0;
  std::function<bool()>
      cancelled; // optional cooperative cancellation, checked between LM steps
  bool robust = false;
  double tolerance =
      0; // Explicit symmetric fractional nominal-value box (0 = Exact).
  double dcrAbsoluteTolerance = 0; // permits refinement of nominal zero DCR
  double relativeFloor = 1e-9, equivalenceTolerance = 1e-6;
  double rMin = 1e-3, rMax = 1e7, lMin = 1e-10, lMax = 10, cMin = 1e-13,
         cMax = 1e-3, dcrMax = 1e7;
  std::vector<Eigen::Matrix2d>
      covariance; // empty => relative weights; SPD otherwise
};
enum class SolveStatus { OK, PORT_OPEN, SINGULAR, ILL_CONDITIONED, NONFINITE };
std::string name(SolveStatus);
enum class ReductionPolicy {
  None,           // keep the physical graph, identity groups, leaf domains
  ExactElectrical // R0 dead zone + exact series/parallel reduction
};
struct PreparedNetwork {
  Graph original;
  Graph effective;
  std::vector<EdgeDomain> domains; // aligned 1:1 with effective edges
  Reduction reduction;
};
// Sole constructor of continuous-fit model domains; fit() consumes these
// domains instead of re-deriving single-device bounds per edge type.
PreparedNetwork prepareForFit(const Graph &, const Config &,
                              ReductionPolicy policy);
struct Forward {
  Complex z = 0;
  std::vector<Complex>
      jacobian; // physical parameters: edge value, then DCR for L
  SolveStatus status = SolveStatus::OK;
  double backwardError = 0, rcond = 1;
};
struct Metrics {
  double rss = inf, wrmse = inf, maxRel = inf;
  std::optional<double> aicc;
};
struct ParameterDiagnostic {
  int id = -1; // stable optimizer parameter id (Model.params index)
  int edge = -1; // effective graph edge index
  ParamQuantity quantity = ParamQuantity::Value; // Value / Dcr
  char kind = 'R';
  double value = 0;
  double lower = 0, upper = 0; // physical admissible interval
  bool free = true, fixed = false, weak = false, atBound = false;
  std::optional<double> standardError;
  std::optional<std::array<double, 2>> ci95;
};
struct Diagnostics {
  std::string optimizer = "not_run", verdict = "HYPOTHESIS_LIMITED";
  int rank = 0, starts = 0, bestStart = -1, convergedStarts = 0,
      agreeingStarts = 0, outliers = 0;
  bool robustUsed = false;
  double condition = inf, worstBackwardError = 0, worstRcond = 1;
  std::vector<double> singularValues, standardErrors;
  std::vector<std::array<double, 2>> confidenceIntervals95;
  std::vector<int> weak, atBound;
  // Independent diagnostic dimensions; verdict stays a derived summary.
  std::string numericalStatus, identifiabilityStatus; // OK/WARN/FAIL, FULL_RANK/...
  double fitObjective = inf; // objective of the final optimization round
  std::vector<ParameterDiagnostic> parameters; // explicit id/edge/quantity map
};
struct Candidate {
  Graph graph;
  Metrics metrics;
  Diagnostics diagnostics;
  std::vector<Complex> predicted;
  Reduction reduction;
  std::string topology, engine = "A";
  size_t members = 1;
  int nParams = 0;
  bool refined = false;
  // Enumeration identity vs electrically reduced identity (Try2.5/Try3).
  std::string originalTopologyKey, effectiveTopologyKey;
  int effectiveDevices = 0;
};
struct SearchResult {
  int which = 0;
  std::string family, mode, termination = "complete";
  bool enumerationComplete = true, continuousGlobalCertified = false;
  size_t generated = 0, structures = 0, evaluated = 0, numericalFailures = 0;
  double elapsed = 0;
  std::vector<Candidate> candidates;
};
Data loadMeasurements(std::istream &);
Data loadCsv(std::istream &); // separate convenience adapter, never changes
                              // strict format
void dumpMeasurements(std::ostream &, const Data &);
int loadCount(std::istream &);
std::vector<Edge> loadComponents(std::istream &);
Graph loadTopology(std::istream &);
void validate(const Data &, const Config &);
void validate(const Graph &);
Upper adjacency(const Graph &);
void printAdjacency(std::ostream &, const Graph &, int rank = 1);
Complex impedance(const Edge &, double frequency);
std::vector<int> liveEdges(const Graph &);
EdgeDomain edgeDomain(const Edge &, const Config &);
Interval bounds(const ReductionExpr &, const std::vector<EdgeDomain> &source);
double evaluate(const ReductionExpr &, const Graph &source);
Reduction reduce(const Graph &, const Config & = {});
std::string canonical(const Graph &, bool values = true);
Forward forward(const Graph &, double frequency, bool jacobian = true);
Metrics metrics(const Data &, const std::vector<Complex> &, int parameters,
                const Config &);
Candidate fit(const PreparedNetwork &, const Data &, const Config &,
              const std::vector<Graph> &initial = {});
// Convenience overload; search layers must call the PreparedNetwork version.
Candidate fit(const Graph &, const Data &, const Config &,
              const std::vector<Graph> &initial = {});
using GraphVisitor = std::function<bool(const Graph &)>;
// false visitor return aborts enumeration; completed return value is explicit.
bool enumerate(const std::vector<Edge> &, const GraphVisitor &,
               const std::function<bool()> &stop = {},
               size_t *structures = nullptr);
std::vector<Graph> spLibrary(int devices, int depth,
                             const std::function<bool()> &stop = {});
std::vector<Graph> rationalStarts(const Data &, int maxDevices);
SearchResult try1(const Data &, const Config & = {});
SearchResult try2(const Data &, const std::vector<Edge> &, const Config & = {});
SearchResult try3(const Data &, const Graph &, const Config & = {});
SearchResult try25(const Data &, const std::vector<char> &,
                   const Config & = {});
void report(std::ostream &, const SearchResult &, const Data &, const Config &,
            bool json = false);
} // namespace lcr
