#pragma once
// Series-parallel RLC one-port topology trees — C++ port of rlc_id/circuits.py
// (DESIGN.md section 4.1).  Canonical rules R1/R2/R3 and the forward-mode AD
// Jacobian (section 5.2) are reproduced one-to-one.

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
// canonical string (R3); leaves carry a single element kind 'R'/'L'/'C'.
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
// map any tree to its R1/R2/R3 representative
TreePtr normalize(const TreePtr& t);

int nLeaves(const TreePtr& t);
std::vector<char> leafKinds(const TreePtr& t);
int maxInternalDepth(const TreePtr& t);

// per-parameter log10 bounds (KIND_BOUNDS of circuits.py)
std::pair<double, double> kindBounds(char kind);
void thetaBounds(const TreePtr& t, std::vector<double>& lb, std::vector<double>& ub);
double clipKind(char kind, double value);  // clip a linear value into the domain

// --- impedance evaluation --------------------------------------------------
// values: linear element values in canonical traversal order
void evalValues(const TreePtr& t, const std::vector<double>& values,
                const Complex* s, size_t m, Complex* out);
// theta: log10 of the element values
void evalTheta(const TreePtr& t, const std::vector<double>& theta,
               const Complex* s, size_t m, Complex* out);
void evalThetaFreq(const TreePtr& t, const std::vector<double>& theta,
                   const double* f, size_t m, Complex* out);

// Forward-mode AD Jacobian (exact, machine precision).
// Fills outZ[m] with Z(jw) and J[i*m+k] = dZ_k/dtheta_i.
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
