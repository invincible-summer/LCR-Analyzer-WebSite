#include "circuits.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <sstream>

namespace rlc {
namespace {

// KIND_BOUNDS of circuits.py: log10 search domain per element kind, plus the
// v2 DCR domain ('D' = series DC resistance of an L device).
const std::map<char, std::pair<double, double>>& kindBoundsTable() {
    static const std::map<char, std::pair<double, double>> tbl = {
        {'R', {-3.0, 7.0}},    // 1 mOhm .. 10 MOhm
        {'L', {-10.0, 1.0}},   // 100 pH .. 10 H
        {'C', {-13.0, -3.0}},  // 0.1 pF .. 1 mF
        {'D', {-6.0, 7.0}},    // 1 uOhm .. 10 MOhm (inductor DCR)
    };
    return tbl;
}

struct EvalCtx {
    const double* values;
    const Complex* s;
    size_t m;
};

// Python's evaluate: PAR collects admittances ys = [1/z_c], sums them
// left-to-right over the sorted children, then takes the reciprocal.
// An L device consumes two values [L, Rd]: Z = Rd + s*L.
void evalRec(const Tree* t, EvalCtx& ctx, size_t& idx, Complex* out) {
    if (t->isLeaf) {
        switch (t->elem) {
        case 'R': {
            const double v = ctx.values[idx++];
            for (size_t k = 0; k < ctx.m; ++k) out[k] = Complex(v, 0.0);
            return;
        }
        case 'L': {
            const double l = ctx.values[idx];
            const double rd = ctx.values[idx + 1];
            idx += 2;
            for (size_t k = 0; k < ctx.m; ++k) out[k] = rd + ctx.s[k] * l;
            return;
        }
        default: {
            const double v = ctx.values[idx++];
            for (size_t k = 0; k < ctx.m; ++k)
                out[k] = Complex(1.0, 0.0) / (ctx.s[k] * v);
            return;
        }
        }
    }
    std::vector<Complex> zc(ctx.m);
    if (t->kind == NK::Ser) {
        bool first = true;
        for (const auto& c : t->kids) {
            evalRec(c.get(), ctx, idx, zc.data());
            if (first) {
                std::copy(zc.begin(), zc.end(), out);
                first = false;
            } else {
                for (size_t k = 0; k < ctx.m; ++k) out[k] = out[k] + zc[k];
            }
        }
    } else {
        bool first = true;
        for (const auto& c : t->kids) {
            evalRec(c.get(), ctx, idx, zc.data());
            if (first) {
                for (size_t k = 0; k < ctx.m; ++k)
                    out[k] = Complex(1.0, 0.0) / zc[k];
                first = false;
            } else {
                for (size_t k = 0; k < ctx.m; ++k)
                    out[k] = out[k] + Complex(1.0, 0.0) / zc[k];
            }
        }
        for (size_t k = 0; k < ctx.m; ++k) out[k] = Complex(1.0, 0.0) / out[k];
    }
}

}  // namespace

TreePtr Tree::makeLeaf(char kind) {
    return std::make_shared<const Tree>(Tree{true, NK::Ser, kind, {}});
}

TreePtr Tree::makeNode(NK kind, std::vector<TreePtr> children) {
    std::stable_sort(children.begin(), children.end(),
                     [](const TreePtr& a, const TreePtr& b) {
                         return canonical(a) < canonical(b);
                     });
    Tree t;
    t.isLeaf = false;
    t.kind = kind;
    t.kids = std::move(children);
    return std::make_shared<const Tree>(std::move(t));
}

std::string canonical(const TreePtr& t) {
    if (t->isLeaf) return std::string(1, t->elem);
    std::vector<std::string> parts;
    parts.reserve(t->kids.size());
    for (const auto& c : t->kids) parts.push_back(canonical(c));
    std::sort(parts.begin(), parts.end());
    std::string body;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) body += ',';
        body += parts[i];
    }
    return std::string(t->kind == NK::Ser ? "S(" : "P(") + body + ")";
}

TreePtr normalize(const TreePtr& tree) {
    if (tree->isLeaf) return tree;
    std::vector<TreePtr> flat;
    for (const auto& child : tree->kids) {
        TreePtr c = normalize(child);
        if (!c->isLeaf && c->kind == tree->kind) {  // R1 flatten
            for (const auto& g : c->kids) flat.push_back(g);
        } else {
            flat.push_back(c);
        }
    }
    // R2': one leaf per kind, except parallel L leaves (second-order tanks)
    // which are kept as distinct devices; R4: a series R leaf folds into a
    // sibling L leaf's DC resistance.
    std::vector<TreePtr> kept;
    bool haveR = false, haveL = false;
    std::vector<TreePtr> subs;
    for (const auto& c : flat) {
        if (!c->isLeaf) {
            subs.push_back(c);
            continue;
        }
        if (c->elem == 'L') {
            haveL = true;
            if (tree->kind == NK::Par) {  // parallel L leaves never merge
                kept.push_back(c);
                continue;
            }
        }
        if (c->elem == 'R') haveR = true;
        bool dup = false;
        for (const auto& k : kept) {
            if (k->isLeaf && k->elem == c->elem) {  // R2' keep first per kind
                dup = true;
                break;
            }
        }
        if (!dup) kept.push_back(c);
    }
    if (tree->kind == NK::Ser && haveR && haveL) {  // R4 series absorption
        std::vector<TreePtr> noR;
        for (const auto& c : kept) {
            if (!(c->isLeaf && c->elem == 'R')) noR.push_back(c);
        }
        kept = std::move(noR);
    }
    std::vector<TreePtr> children = std::move(kept);
    for (const auto& s : subs) children.push_back(s);
    if (children.size() == 1) return children[0];
    return Tree::makeNode(tree->kind, std::move(children));
}

int nLeaves(const TreePtr& t) {
    if (t->isLeaf) return 1;
    int n = 0;
    for (const auto& c : t->kids) n += nLeaves(c);
    return n;
}

int nParams(const TreePtr& t) {
    if (t->isLeaf) return nParamsOfLeaf(t->elem);
    int n = 0;
    for (const auto& c : t->kids) n += nParams(c);
    return n;
}

int nParamsOfLeaf(char kind) { return kind == 'L' ? 2 : 1; }

std::vector<char> leafKinds(const TreePtr& t) {
    std::vector<char> out;
    std::function<void(const Tree*)> rec = [&](const Tree* p) {
        if (p->isLeaf) {
            out.push_back(p->elem);
            return;
        }
        for (const auto& c : p->kids) rec(c.get());
    };
    rec(t.get());
    return out;
}

std::vector<char> paramKinds(const TreePtr& t) {
    std::vector<char> out;
    std::function<void(const Tree*)> rec = [&](const Tree* p) {
        if (p->isLeaf) {
            out.push_back(p->elem);
            if (p->elem == 'L') out.push_back('D');  // the Rd parameter
            return;
        }
        for (const auto& c : p->kids) rec(c.get());
    };
    rec(t.get());
    return out;
}

int maxInternalDepth(const TreePtr& t) {
    if (t->isLeaf) return 0;
    int d = 0;
    for (const auto& c : t->kids) d = std::max(d, maxInternalDepth(c));
    return 1 + d;
}

std::pair<double, double> kindBounds(char kind) {
    auto it = kindBoundsTable().find(kind);
    if (it == kindBoundsTable().end()) throw std::invalid_argument("bad leaf kind");
    return it->second;
}

void thetaBounds(const TreePtr& t, std::vector<double>& lb, std::vector<double>& ub) {
    auto kinds = paramKinds(t);
    lb.clear();
    ub.clear();
    lb.reserve(kinds.size());
    ub.reserve(kinds.size());
    for (char k : kinds) {
        auto b = kindBounds(k);
        lb.push_back(b.first);
        ub.push_back(b.second);
    }
}

double clipKind(char kind, double value) {
    auto b = kindBounds(kind);
    return std::min(std::max(value, std::pow(10.0, b.first)), std::pow(10.0, b.second));
}

void evalValues(const TreePtr& t, const std::vector<double>& values,
                const Complex* s, size_t m, Complex* out) {
    EvalCtx ctx{values.data(), s, m};
    size_t idx = 0;
    evalRec(t.get(), ctx, idx, out);
}

void evalTheta(const TreePtr& t, const std::vector<double>& theta,
               const Complex* s, size_t m, Complex* out) {
    std::vector<double> values(theta.size());
    for (size_t i = 0; i < theta.size(); ++i) values[i] = std::pow(10.0, theta[i]);
    evalValues(t, values, s, m, out);
}

void evalThetaFreq(const TreePtr& t, const std::vector<double>& theta,
                   const double* f, size_t m, Complex* out) {
    std::vector<Complex> s(m);
    for (size_t k = 0; k < m; ++k) s[k] = Complex(0.0, 1.0) * (2.0 * M_PI * f[k]);
    evalTheta(t, theta, s.data(), m, out);
}

// Forward AD: rec fills z (m) and J rows [i0, i0+nParams(t)); returns z.
// Leaf rows: dZ/dtheta = dZ/dv * ln(10) * v; an L device owns TWO rows
// (dZ/dlogL = s*ln10*L at i, dZ/dlogRd = ln10*Rd at i+1).  SER rows are
// disjoint sums.  PAR rows: dZ = Z^2 * sum dZc / Zc^2, applied by scaling
// each child's row block.
namespace {
std::vector<Complex> jacRec(const Tree* t, const std::vector<double>& values,
                            const Complex* s, size_t m, size_t& idx, size_t i0,
                            Complex* J) {
    if (t->isLeaf) {
        Complex* row = J + i0 * m;
        switch (t->elem) {
        case 'R': {
            const size_t i = idx++;
            const double v = values[i];
            std::vector<Complex> z(m, Complex(v, 0.0));
            for (size_t k = 0; k < m; ++k) row[k] = Complex(kLn10 * v, 0.0);
            return z;
        }
        case 'L': {
            const size_t il = idx;
            const size_t ird = idx + 1;
            idx += 2;
            const double l = values[il], rd = values[ird];
            Complex* rowRd = J + (i0 + 1) * m;
            std::vector<Complex> z(m);
            for (size_t k = 0; k < m; ++k) {
                z[k] = rd + s[k] * l;
                row[k] = s[k] * (kLn10 * l);        // dZ/dlogL
                rowRd[k] = Complex(kLn10 * rd, 0.0);  // dZ/dlogRd
            }
            return z;
        }
        default: {
            const size_t i = idx++;
            const double v = values[i];
            std::vector<Complex> z(m);
            for (size_t k = 0; k < m; ++k) {
                z[k] = Complex(1.0, 0.0) / (s[k] * v);
                Complex dz = Complex(-1.0, 0.0) / (s[k] * v * v);
                row[k] = dz * (kLn10 * v);
            }
            return z;
        }
        }
    }
    std::vector<Complex> z(m);
    size_t ci = i0;
    if (t->kind == NK::Ser) {
        bool first = true;
        for (const auto& c : t->kids) {
            auto zc = jacRec(c.get(), values, s, m, idx, ci, J);
            ci += (size_t)nParams(c);
            if (first) {
                z = zc;
                first = false;
            } else {
                for (size_t k = 0; k < m; ++k) z[k] = z[k] + zc[k];
            }
        }
        return z;
    }
    // PAR: accumulate Y = sum 1/zc, then scale each child's rows by z^2/zc^2
    std::vector<Complex> Y(m);
    std::vector<std::vector<Complex>> ycs;
    bool first = true;
    for (const auto& c : t->kids) {
        auto zc = jacRec(c.get(), values, s, m, idx, ci, J);
        std::vector<Complex> yc(m);
        for (size_t k = 0; k < m; ++k) yc[k] = Complex(1.0, 0.0) / zc[k];
        if (first) {
            Y = yc;
            first = false;
        } else {
            for (size_t k = 0; k < m; ++k) Y[k] = Y[k] + yc[k];
        }
        ycs.push_back(std::move(yc));
        ci += (size_t)nParams(c);
    }
    for (size_t k = 0; k < m; ++k) z[k] = Complex(1.0, 0.0) / Y[k];
    size_t r = i0;
    for (size_t c = 0; c < t->kids.size(); ++c) {
        size_t nr = (size_t)nParams(t->kids[c]);
        for (size_t i = 0; i < nr; ++i) {
            Complex* row = J + (r + i) * m;
            for (size_t k = 0; k < m; ++k) {
                Complex f = (z[k] * z[k]) * (ycs[c][k] * ycs[c][k]);
                row[k] = row[k] * f;
            }
        }
        r += nr;
    }
    return z;
}
}  // namespace

void evalJac(const TreePtr& t, const std::vector<double>& theta,
             const Complex* s, size_t m, Complex* outZ, Complex* J) {
    const size_t p = theta.size();
    std::vector<double> values(p);
    for (size_t i = 0; i < p; ++i) values[i] = std::pow(10.0, theta[i]);
    std::fill(J, J + p * m, Complex(0.0, 0.0));
    size_t idx = 0;
    auto z = jacRec(t.get(), values, s, m, idx, 0, J);
    std::copy(z.begin(), z.end(), outZ);
}

Assembled assemble(NK kind, std::vector<Assembled> children) {
    std::stable_sort(children.begin(), children.end(),
                     [](const Assembled& a, const Assembled& b) {
                         return canonical(a.tree) < canonical(b.tree);
                     });
    std::vector<TreePtr> trees;
    std::vector<double> values;
    for (auto& c : children) {
        trees.push_back(c.tree);
        values.insert(values.end(), c.values.begin(), c.values.end());
    }
    return Assembled{Tree::makeNode(kind, std::move(trees)), std::move(values)};
}

// SI prefix table of circuits.py
std::string fmtEng(double x) {
    static const std::pair<int, const char*> prefixes[] = {
        {9, "G"}, {6, "M"}, {3, "k"}, {0, ""}, {-3, "m"}, {-6, "u"},
        {-9, "n"}, {-12, "p"}, {-15, "f"}};
    if (x == 0.0 || !std::isfinite(x)) {
        std::ostringstream os;
        os << x;
        return os.str();
    }
    double ax = std::fabs(x);
    for (const auto& pr : prefixes) {
        if (ax >= std::pow(10.0, (double)pr.first)) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.4g%s", x / std::pow(10.0, (double)pr.first),
                          pr.second);
            return buf;
        }
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4g", x);
    return buf;
}

std::string toString(const TreePtr& tree, const std::vector<double>* theta) {
    std::vector<double> vals;
    const std::vector<double>* vp = nullptr;
    if (theta) {
        vals.resize(theta->size());
        for (size_t i = 0; i < theta->size(); ++i) vals[i] = std::pow(10.0, (*theta)[i]);
        vp = &vals;
    }
    size_t idx = 0;
    std::function<std::string(const Tree*)> fmt = [&](const Tree* t) -> std::string {
        if (t->isLeaf) {
            if (!vp) return std::string(1, t->elem);
            if (t->elem == 'L') {
                double l = vp->at(idx);
                double rd = vp->at(idx + 1);
                idx += 2;
                return std::string("L(") + fmtEng(l) + ", Rd " + fmtEng(rd) + ")";
            }
            double v = vp->at(idx++);
            return std::string(1, t->elem) + "(" + fmtEng(v) + ")";
        }
        const char* sep = t->kind == NK::Ser ? " + " : " || ";
        std::string out;
        for (size_t i = 0; i < t->kids.size(); ++i) {
            if (i) out += sep;
            const Tree* c = t->kids[i].get();
            std::string s = fmt(c);
            if (!c->isLeaf)
                out += "(" + s + ")";
            else
                out += s;
        }
        return out;
    };
    return fmt(tree.get());
}

}  // namespace rlc
