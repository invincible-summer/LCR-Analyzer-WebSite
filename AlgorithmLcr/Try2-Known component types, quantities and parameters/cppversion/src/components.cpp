#include "components.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace ng {

namespace {
constexpr double kPow[] = {1e9, 1e6, 1e3, 1e0, 1e-3, 1e-6, 1e-9, 1e-12};
constexpr const char* kPrefix[] = {"G", "M", "k", "", "m", "u", "n", "p"};
}  // namespace

std::string pyRepr(double v) {
    if (v == 0.0) return std::signbit(v) ? "-0.0" : "0.0";
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    // shortest round-trip digits via to_chars scientific: "d.ddddde±dd"
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::scientific);
    std::string sci(buf, res.ptr);  // e.g. "3.382342058129827e-05"
    bool neg = sci[0] == '-';
    size_t ep = sci.find('e');
    std::string mant = sci.substr(neg ? 1 : 0, ep - (neg ? 1 : 0));  // "d.ddd"
    int exp10 = std::atoi(sci.c_str() + ep + 1);
    std::string digits = mant;
    size_t dot = digits.find('.');
    if (dot != std::string::npos) digits.erase(dot, 1);  // "dddd" without dot
    // decimal point position after the first digit
    int decPos = exp10 + 1;
    std::string sign = neg ? "-" : "";
    if (exp10 >= -4 && exp10 <= 15) {  // Python: fixed for 1e-4 <= |v| < 1e16
        std::string out;
        if (decPos <= 0) {
            out = "0." + std::string((size_t)(-decPos), '0') + digits;
        } else if ((size_t)decPos >= digits.size()) {
            out = digits + std::string((size_t)decPos - digits.size(), '0') + ".0";
        } else {
            out = digits.substr(0, (size_t)decPos) + "." + digits.substr((size_t)decPos);
        }
        return sign + out;
    }
    // scientific: mantissa with at least one digit, exponent >= 2 digits
    std::string m(digits.substr(0, 1));
    if (digits.size() > 1) m += "." + digits.substr(1);
    char ebuf[16];
    std::snprintf(ebuf, sizeof(ebuf), "%+03d", exp10);  // e-05 / e+20 style
    return sign + m + "e" + ebuf;
}

std::string engFormat3(double v) {
    if (v == 0.0) return "0";
    double a = std::fabs(v);
    for (int i = 0; i < 8; ++i) {
        if (a >= kPow[i]) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%.3g%s", v / kPow[i], kPrefix[i]);
            return buf;
        }
    }
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.3g", v);
    return buf;
}

bool Component::operator<(const Component& o) const {
    if (kind != o.kind) return kind < o.kind;  // 'C' < 'L' < 'R'
    if (value != o.value) return value < o.value;
    return dcr < o.dcr;
}

bool Component::operator==(const Component& o) const {
    return kind == o.kind && value == o.value && dcr == o.dcr;
}

std::string Component::label() const {
    char buf[96];
    if (kind == 'R') {
        std::snprintf(buf, sizeof(buf), "R(%sohm)", engFormat3(value).c_str());
    } else if (kind == 'C') {
        std::snprintf(buf, sizeof(buf), "C(%sF)", engFormat3(value).c_str());
    } else if (dcr == 0.0) {
        std::snprintf(buf, sizeof(buf), "L(%sH)", engFormat3(value).c_str());
    } else {
        std::snprintf(buf, sizeof(buf), "L(%sH+d%sohm)", engFormat3(value).c_str(),
                      engFormat3(dcr).c_str());
    }
    return buf;
}

Component makeComponent(char kind, double value, double dcr) {
    if (kind != 'R' && kind != 'C' && kind != 'L')
        throw std::invalid_argument(std::string("kind must be one of R|L|C, got '") +
                                    kind + "'");
    if (!(value > 0.0)) throw std::invalid_argument("value must be positive");
    if (dcr < 0.0) throw std::invalid_argument("dcr must be >= 0");
    return Component{kind, value, dcr};
}

ComponentSet::ComponentSet(std::vector<Component> comps) {
    if (comps.empty()) throw std::invalid_argument("need at least one component");
    std::sort(comps.begin(), comps.end());
    components_ = std::move(comps);
}

int ComponentSet::count(char kind) const {
    int k = 0;
    for (const auto& c : components_)
        if (c.kind == kind) ++k;
    return k;
}

int ComponentSet::nParams() const {
    int p = 0;
    for (const auto& c : components_) p += (c.kind == 'L') ? 2 : 1;
    return p;
}

bool ComponentSet::distinguishable() const {
    for (size_t i = 1; i < components_.size(); ++i)
        if (components_[i] == components_[i - 1]) return false;
    return true;
}

std::vector<std::string> ComponentSet::labels() const {
    std::vector<std::string> out;
    out.reserve(components_.size());
    for (const auto& c : components_) out.push_back(c.label());
    return out;
}

ComponentSet ComponentSet::make(const std::vector<double>& rs,
                                const std::vector<double>& cs,
                                const std::vector<std::pair<double, double>>& ls) {
    std::vector<Component> comps;
    comps.reserve(rs.size() + cs.size() + ls.size());
    for (double r : rs) comps.push_back(makeComponent('R', r));
    for (double c : cs) comps.push_back(makeComponent('C', c));
    for (const auto& ld : ls) comps.push_back(makeComponent('L', ld.first, ld.second));
    return ComponentSet(std::move(comps));
}

std::complex<double> edgeAdmittance(const Component& c, std::complex<double> s) {
    if (c.kind == 'R') return std::complex<double>(1.0 / c.value, 0.0);
    if (c.kind == 'C') return s * c.value;
    return 1.0 / (std::complex<double>(c.dcr, 0.0) + s * c.value);
}

}  // namespace ng
