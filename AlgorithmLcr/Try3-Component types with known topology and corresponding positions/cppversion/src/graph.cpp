#include "graph.hpp"

#include <algorithm>
#include <cstdio>
#include <numeric>
#include <set>

namespace tf {

namespace {

Expr makeLeaf(int idx, char kind) {
    Expr e;
    e.leaf = true;
    e.edgeIdx = idx;
    e.kind = kind;
    return e;
}

Expr makeAgg(char tag, std::vector<Expr> children, char kind) {
    Expr e;
    e.leaf = false;
    e.tag = tag;
    e.kind = kind;
    e.children = std::move(children);
    return e;
}

struct WEdge {
    int u = 0, v = 0;
    char kind = 'R';
    Expr expr;
    std::vector<int> members;
};

// union-find over the nodes touched by the working edges (+ ports)
std::map<int, int> componentsOf(const std::set<int>& nodes, const std::vector<WEdge>& wedges) {
    std::map<int, int> parent;
    for (int n : nodes) parent[n] = n;
    auto find = [&parent](int a) {
        while (parent[a] != a) {
            parent[a] = parent[parent[a]];
            a = parent[a];
        }
        return a;
    };
    for (const auto& e : wedges) {
        int ra = find(e.u), rb = find(e.v);
        if (ra != rb) parent[ra] = rb;
    }
    std::map<int, int> comp;
    for (int n : nodes) comp[n] = find(n);
    return comp;
}

}  // namespace

Value evalGroup(const Expr& expr, const std::vector<Value>& origVals) {
    if (expr.leaf) return origVals[(size_t)expr.edgeIdx];
    std::vector<Value> vals;
    vals.reserve(expr.children.size());
    for (const auto& ch : expr.children) vals.push_back(evalGroup(ch, origVals));
    const char kind = expr.kind;
    if (expr.tag == 'p') {  // parallel
        if (kind == 'R') {
            double g = 0.0;
            for (const auto& val : vals) g += 1.0 / val.v1;
            return Value{'R', 1.0 / g, 0.0};
        }
        double c = 0.0;
        for (const auto& val : vals) c += val.v1;
        return Value{'C', c, 0.0};
    }
    // series
    if (kind == 'R') {
        double r = 0.0;
        for (const auto& val : vals) r += val.v1;
        return Value{'R', r, 0.0};
    }
    if (kind == 'C') {
        double s = 0.0;
        for (const auto& val : vals) s += 1.0 / val.v1;
        return Value{'C', 1.0 / s, 0.0};
    }
    double lTot = 0.0, rdTot = 0.0;
    for (const auto& val : vals) {
        if (val.kind == 'L') {
            lTot += val.v1;
            rdTot += val.v2;
        } else if (val.kind == 'R') {
            rdTot += val.v1;
        }
    }
    return Value{'L', lTot, rdTot};
}

std::vector<Value> ReductionResult::groupValues(
    const std::vector<Value>& origVals) const {
    std::vector<Value> out;
    out.reserve(edges.size());
    for (const auto& e : edges) out.push_back(evalGroup(e.expr, origVals));
    return out;
}

std::string ReductionResult::describe() const {
    std::string out;
    for (size_t g = 0; g < edges.size(); ++g) {
        const auto& e = edges[g];
        char buf[96];
        std::string mem;
        for (size_t q = 0; q < e.members.size(); ++q) {
            if (q) mem += ",";
            mem += std::to_string(e.members[q]);
        }
        std::snprintf(buf, sizeof(buf), "g%zu:%c[%d-%d]{%s} ", g, e.kind, e.u, e.v,
                      mem.c_str());
        out += buf;
    }
    for (const auto& [i, why] : dropped) {
        out += "e" + std::to_string(i) + ":dropped(" + why + ") ";
    }
    if (!out.empty()) out.pop_back();
    return out;
}

namespace {

void dropDangling(std::vector<WEdge>& work, std::map<int, std::string>& dropped,
                  std::set<int>& nodes, bool& changedAny) {
    while (true) {
        std::map<int, int> deg;
        for (int n : nodes) deg[n] = 0;
        for (const auto& e : work) {
            deg[e.u] += 1;
            deg[e.v] += 1;
        }
        int victimNode = -1;
        const WEdge* victimEdge = nullptr;
        for (const auto& [n, d] : deg) {  // ascending node label
            if (n != 0 && n != 1 && d == 1) {
                for (const auto& e : work)
                    if (e.u == n || e.v == n) {
                        victimNode = n;
                        victimEdge = &e;
                        break;
                    }
                break;  // only the FIRST such node is considered per pass
            }
        }
        if (victimNode < 0) return;
        // copy the victim's data first: removing it from  frees the
        // buffer the pointer refers into
        std::vector<int> victimMembers = victimEdge->members;
        std::vector<WEdge> next;
        bool removed = false;
        for (const auto& e : work) {
            if (!removed && &e == victimEdge) {
                removed = true;
                continue;
            }
            next.push_back(e);
        }
        work = std::move(next);
        nodes.erase(victimNode);
        for (int m : victimMembers) dropped[m] = "dangling";
        changedAny = true;
    }
}

}  // namespace

ReductionResult reduceGraph(const std::vector<std::tuple<int, int, char>>& edges) {
    if (edges.empty()) throw PortOpenError("empty edge list");
    for (const auto& [u, v, k] : edges) {
        (void)u;
        (void)v;
        if (k != 'R' && k != 'C' && k != 'L')
            throw std::invalid_argument(std::string("kind must be one of R|L|C, got '") +
                                        k + "'");
    }
    std::vector<WEdge> work;
    for (size_t i = 0; i < edges.size(); ++i) {
        auto [u, v, k] = edges[i];
        WEdge e;
        e.u = u;
        e.v = v;
        e.kind = k;
        e.expr = makeLeaf((int)i, k);
        e.members = {(int)i};
        work.push_back(std::move(e));
    }
    std::map<int, std::string> dropped;
    int nPasses = 0;
    bool changed = true;
    while (changed) {
        changed = false;
        ++nPasses;

        // -- F1a: self loops ----------------------------------------------
        {
            std::vector<WEdge> keep;
            for (const auto& e : work) {
                if (e.u == e.v) {
                    for (int m : e.members) dropped[m] = "self-loop";
                    changed = true;
                } else {
                    keep.push_back(e);
                }
            }
            work = std::move(keep);
        }

        // -- F1b: connectivity of the port --------------------------------
        {
            std::set<int> nodes;
            for (const auto& e : work) {
                nodes.insert(e.u);
                nodes.insert(e.v);
            }
            nodes.insert(0);
            nodes.insert(1);
            std::map<int, int> comp = componentsOf(nodes, work);
            int root0 = comp[0];
            if (comp[1] != root0)
                throw PortOpenError("nodes 0 and 1 are not connected");
            std::vector<WEdge> keep;
            for (const auto& e : work) {
                if (comp[e.u] == root0) {
                    keep.push_back(e);
                } else {
                    for (int m : e.members) dropped[m] = "disconnected";
                    changed = true;
                }
            }
            work = std::move(keep);
        }

        // -- F1c: dangling branches ----------------------------------------
        {
            std::set<int> nodes;
            for (const auto& e : work) {
                nodes.insert(e.u);
                nodes.insert(e.v);
            }
            nodes.insert(0);
            nodes.insert(1);
            bool ch = false;
            dropDangling(work, dropped, nodes, ch);
            changed = changed || ch;
        }

        // -- F2: same-kind parallel merges (R, C only) ---------------------
        {
            // groups keyed by normalized pair, first-encounter order
            std::vector<std::pair<std::pair<int, int>, std::vector<const WEdge*>>> groups;
            std::map<std::pair<int, int>, int> groupIdx;
            for (const auto& e : work) {
                auto pr = e.u <= e.v ? std::make_pair(e.u, e.v) : std::make_pair(e.v, e.u);
                auto it = groupIdx.find(pr);
                if (it == groupIdx.end()) {
                    groupIdx[pr] = (int)groups.size();
                    groups.push_back({pr, {&e}});
                } else {
                    groups[(size_t)it->second].second.push_back(&e);
                }
            }
            std::vector<WEdge> merged;
            for (const auto& [pr, es] : groups) {
                // by-kind buckets in first-encounter order
                std::vector<std::pair<char, std::vector<const WEdge*>>> byKind;
                std::map<char, int> kindIdx;
                for (const WEdge* e : es) {
                    auto it = kindIdx.find(e->kind);
                    if (it == kindIdx.end()) {
                        kindIdx[e->kind] = (int)byKind.size();
                        byKind.push_back({e->kind, {e}});
                    } else {
                        byKind[(size_t)it->second].second.push_back(e);
                    }
                }
                for (const auto& [kind, same] : byKind) {
                    if ((kind == 'R' || kind == 'C') && same.size() > 1) {
                        std::vector<Expr> children;
                        std::vector<int> members;
                        for (const WEdge* e : same) {
                            children.push_back(e->expr);
                            members.insert(members.end(), e->members.begin(),
                                           e->members.end());
                        }
                        WEdge ne;
                        ne.u = pr.first;
                        ne.v = pr.second;
                        ne.kind = kind;
                        ne.expr = makeAgg('p', std::move(children), kind);
                        ne.members = std::move(members);
                        merged.push_back(std::move(ne));
                        changed = true;
                    } else {
                        for (const WEdge* e : same) merged.push_back(*e);
                    }
                }
            }
            work = std::move(merged);
        }

        // -- F3/F4: series merge at the first eligible degree-2 node -------
        {
            std::map<int, int> deg;
            for (const auto& e : work) {
                deg[e.u] += 1;
                deg[e.v] += 1;
            }
            const WEdge *e1 = nullptr, *e2 = nullptr;
            int aOut = 0, bOut = 0;
            bool found = false;
            for (const auto& [n, d] : deg) {  // ascending label
                if (n == 0 || n == 1 || d != 2) continue;
                std::vector<const WEdge*> inc;
                for (const auto& e : work)
                    if (e.u == n || e.v == n) inc.push_back(&e);
                if (inc.size() != 2) continue;
                e1 = inc[0];
                e2 = inc[1];
                int a = e1->u == n ? e1->v : e1->u;
                int b = e2->u == n ? e2->v : e2->u;
                if (a == b) continue;
                char k1 = e1->kind, k2 = e2->kind;
                bool mergeable = (k1 == k2 && (k1 == 'R' || k1 == 'C' || k1 == 'L')) ||
                                 ((k1 == 'R' && k2 == 'L') || (k1 == 'L' && k2 == 'R'));
                if (mergeable) {
                    aOut = a;
                    bOut = b;
                    found = true;
                    break;
                }
            }
            if (found) {
                char kind = (e1->kind == 'L' || e2->kind == 'L') ? 'L' : e1->kind;
                // copy both edges' payloads first: reassigning  frees
                // the buffer e1/e2 point into
                Expr e1Expr = e1->expr, e2Expr = e2->expr;
                std::vector<int> members = e1->members;
                members.insert(members.end(), e2->members.begin(), e2->members.end());
                std::vector<WEdge> next;
                for (const auto& e : work)
                    if (&e != e1 && &e != e2) next.push_back(e);
                work = std::move(next);
                WEdge ne;
                ne.u = aOut;
                ne.v = bOut;
                ne.kind = kind;
                ne.expr = makeAgg('s', {e1Expr, e2Expr}, kind);
                ne.members = std::move(members);
                work.push_back(std::move(ne));
                changed = true;
            }
        }
    }

    if (work.empty()) throw PortOpenError("no edge influences the port after reduction");
    ReductionResult out;
    for (const auto& e : work) {
        ReducedEdge re;
        re.u = e.u;
        re.v = e.v;
        re.kind = e.kind;
        re.expr = e.expr;
        re.members = e.members;
        out.edges.push_back(std::move(re));
    }
    out.dropped = std::move(dropped);
    out.nPasses = nPasses;
    return out;
}

}  // namespace tf
