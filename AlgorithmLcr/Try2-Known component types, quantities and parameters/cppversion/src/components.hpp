#pragma once
// Known component multiset (Try2 problem input) -- port of
// netgraph_id/components.py (DESIGN.md section 1).
//
// Each component is a 2-terminal edge:
//   * resistor: ideal, one parameter R > 0;
//   * capacitor: ideal, one parameter C > 0;
//   * inductor: ideal L > 0 in series with an ideal DC resistance R_dc >= 0.
//
// Components with identical kind and identical parameters are
// interchangeable; the enumeration must not count their permutations as
// distinct networks.

#include <complex>
#include <string>
#include <vector>

namespace ng {

// Kinds are stored as the letters 'R' | 'C' | 'L'.  Interchangeability keys
// sort by (kind letter, value, dcr); note the letter ordering 'C' < 'L' < 'R'
// reproduces the Python tuple-of-strings ordering used by the reference.
struct Component {
    char kind = 'R';
    double value = 0.0;
    double dcr = 0.0;

    bool isIdealInductor() const { return kind == 'L' && dcr == 0.0; }
    // Interchangeability key ordering (operator< mirrors Python's
    // ("C" < "L" < "R", value, dcr) tuple comparison).
    bool operator<(const Component& o) const;
    bool operator==(const Component& o) const;
    std::string label() const;  // "R(1kohm)" / "L(10mH+d5ohm)" style
};

// throws std::invalid_argument on a bad kind / non-positive value / dcr < 0
Component makeComponent(char kind, double value, double dcr = 0.0);

class ComponentSet {
public:
    // canonical order: sorted by key (kind letter 'C'<'L'<'R', then values)
    explicit ComponentSet(std::vector<Component> comps);

    int n() const { return (int)components_.size(); }             // E
    int count(char kind) const;
    int nParams() const;   // 1 per R/C, 2 per L (constant for all candidates)
    const std::vector<Component>& components() const { return components_; }
    bool distinguishable() const;

    std::vector<std::string> labels() const;

    static ComponentSet make(const std::vector<double>& rs,
                             const std::vector<double>& cs,
                             const std::vector<std::pair<double, double>>& ls);

private:
    std::vector<Component> components_;
};

// Admittance Y_e(s): R: 1/R    C: sC    L: 1/(dcr + sL)
std::complex<double> edgeAdmittance(const Component& c, std::complex<double> s);

// Engineering formatting with 3 significant digits (Python _fmt).
std::string engFormat3(double v);

// Python repr(float): shortest round-trip digits, fixed notation for
// 1e-4 <= |v| < 1e16, scientific otherwise (exponent >= 2 digits).  Used to
// reproduce the reference implementation's str()-based sort keys exactly.
std::string pyRepr(double v);

}  // namespace ng
