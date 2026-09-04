#pragma once
// Series-parallel RLC one-port topology trees — C++ port of rlc_id/circuits.py
// (DESIGN.md section 4.1).  v2 model: an inductor is a REAL inductor (ideal L
// in series with a DC resistance Rd — ONE device, TWO parameters).  Canonical
// rules R1/R2'/R3/R4 and the forward-mode AD Jacobian (section 5.2) are
// reproduced one-to-one:
//   R2'  at most one leaf of each kind per node, EXCEPT parallel L leaves
//        (two (L + Rd) devices in parallel are a second-order tank);
//   R4   a SER node never holds both an R leaf and an L leaf (the series R
//        folds into the inductor's DC resistance — one device).

#include <complex>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rlc {

using Complex = std::complex<double>;

// log(10), used by the log10 parameterization everywhere
constexpr double kLn10 = 2.30258509299404568402;

enum class NK { Ser, Par };

inline NK opposite(NK k) { return k == NK::Ser ? NK::Par : NK::Ser; }
inline const char* nodeKindName(NK k) { return k == NK::Ser ? "SER" : "PAR"; }

struct Tree;
using TreePtr = std::shared_ptr<const Tree>;

// Immutable topology tree.  Internal nodes carry 2+ children stored sorted by
// canonical string (R3); leaves carry a single device kind 'R'/'L'/'C'.
struct Tree {
    bool isLeaf = true;
    NK kind = NK::Ser;          // internal nodes only
    char elem = 'R';            // leaves only
    std::vector<TreePtr> kids;  // internal nodes only

    static TreePtr makeLeaf(char kind);
    static TreePtr makeNode(NK kind, std::vector<TreePtr> children);
};

// canonical serialization (unique per electrical equivalence class)
std::string canonical(const TreePtr& t);
// map any tree to its R1/R2'/R3/R4 representative
TreePtr normalize(const TreePtr& t);

// device count (an L + Rd pair counts as ONE device)
int nLeaves(const TreePtr& t);
// free-parameter count (L devices carry two: L and Rd)
int nParams(const TreePtr& t);
int nParamsOfLeaf(char kind);
std::vector<char> leafKinds(const TreePtr& t);
// parameter kinds in theta order ('R', 'L', 'D'=Rd, 'C'; two entries per L)
std::vector<char> paramKinds(const TreePtr& t);
int maxInternalDepth(const TreePtr& t);

// per-parameter log10 bounds: KIND_BOUNDS of circuits.py plus the DCR domain
// ('D' = series DC resistance of an L device, DCR_BOUNDS).
std::pair<double, double> kindBounds(char kind);  // 'R','L','C','D'
void thetaBounds(const TreePtr& t, std::vector<double>& lb, std::vector<double>& ub);
double clipKind(char kind, double value);  // clip a linear value into the domain

// linear value used for "ideal" inductors (DCR lower bound, 1 uOhm)
constexpr double kDcrMin = 1e-6;

// --- impedance evaluation --------------------------------------------------
// values: linear parameter values in canonical traversal order (two entries
// per L device: [L, Rd])
void evalValues(const TreePtr& t, const std::vector<double>& values,
                const Complex* s, size_t m, Complex* out);
// theta: log10 of the parameter values
void evalTheta(const TreePtr& t, const std::vector<double>& theta,
               const Complex* s, size_t m, Complex* out);
void evalThetaFreq(const TreePtr& t, const std::vector<double>& theta,
                   const double* f, size_t m, Complex* out);

// Forward-mode AD Jacobian (exact, machine precision).
// Fills outZ[m] with Z(jw) and J[i*m+k] = dZ_k/dtheta_i (p = nParams rows).
void evalJac(const TreePtr& t, const std::vector<double>& theta,
             const Complex* s, size_t m, Complex* outZ, Complex* J);

// --- value-carrying construction -------------------------------------------
struct Assembled {
    TreePtr tree;
    std::vector<double> values;  // linear values in the tree's leaf order
};
Assembled assemble(NK kind, std::vector<Assembled> children);

// --- printing ---------------------------------------------------------------
std::string fmtEng(double x);   // SI-prefix engineering format
std::string toString(const TreePtr& t, const std::vector<double>* theta = nullptr);

}  // namespace rlc
