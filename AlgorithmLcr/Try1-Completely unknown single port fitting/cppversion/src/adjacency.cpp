#include "adjacency.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>

namespace rlc {

namespace {

std::string fmtEdge(const Edge& e) {
    char buf[96];
    if (e.type == 'L' && e.dcr != 0.0) {
        std::snprintf(buf, sizeof(buf), "L %.3e dcr %.3e", e.parameter, e.dcr);
    } else {
        std::snprintf(buf, sizeof(buf), "%c %.3e", e.type, e.parameter);
    }
    return buf;
}

}  // namespace

Adjacency::Adjacency(int V) : V_(V) {
    if (V < 2) throw std::invalid_argument("a one-port needs at least the 2 terminal nodes");
    rows_.resize(V);
    for (int i = 0; i < V; ++i) rows_[i].resize(V - 1 - i);
}

std::vector<Edge>& Adjacency::slot(int i, int j) {
    if (!(0 <= i && i < j && j < V_))
        throw std::invalid_argument("slot outside upper triangle");
    return rows_[i][j - i - 1];
}

const std::vector<Edge>& Adjacency::slot(int i, int j) const {
    return const_cast<Adjacency*>(this)->slot(i, j);
}

void Adjacency::add(int i, int j, Edge e) {
    if (i == j) throw std::invalid_argument("self loops are not part of the format");
    if (i > j) std::swap(i, j);
    slot(i, j).push_back(e);
}

int Adjacency::nEdges() const {
    int n = 0;
    for (const auto& row : rows_)
        for (const auto& cell : row) n += (int)cell.size();
    return n;
}

std::vector<std::tuple<int, int, const std::vector<Edge>*>> Adjacency::occupied() const {
    std::vector<std::tuple<int, int, const std::vector<Edge>*>> out;
    for (int i = 0; i < V_; ++i) {
        for (int k = 0; k < (int)rows_[i].size(); ++k) {
            if (!rows_[i][k].empty())
                out.emplace_back(i, i + k + 1, &rows_[i][k]);
        }
    }
    return out;
}

std::string Adjacency::formatBlock(const std::string& label) const {
    std::string head = label.empty()
                           ? std::string("adjacency ")
                           : "adjacency[" + label + "] ";
    std::string out = head + "V=" + std::to_string(V_) + " (ports 0,1):";
    for (const auto& [i, j, edges] : occupied()) {
        out += "\n  (" + std::to_string(i) + "," + std::to_string(j) + "): ";
        for (size_t k = 0; k < edges->size(); ++k) {
            if (k) out += " | ";
            out += fmtEdge((*edges)[k]);
        }
    }
    return out;
}

int nChainNodes(const TreePtr& tree) {
    if (tree->isLeaf) return 0;
    int own = tree->kind == NK::Ser ? (int)tree->kids.size() - 1 : 0;
    for (const auto& c : tree->kids) own += nChainNodes(c);
    return own;
}

Adjacency treeToAdjacency(const TreePtr& tree, const std::vector<double>& theta) {
    std::vector<double> values(theta.size());
    for (size_t i = 0; i < theta.size(); ++i) values[i] = std::pow(10.0, theta[i]);
    if ((int)values.size() != nParams(tree))
        throw std::invalid_argument("theta length does not match the tree's parameter count");

    Adjacency adj(2 + nChainNodes(tree));
    size_t idx = 0;
    int counter = 2;

    std::function<void(const Tree*, int, int)> emit = [&](const Tree* t, int a, int b) {
        if (t->isLeaf) {
            if (t->elem == 'L') {
                adj.add(a, b, Edge{'L', values[idx], values[idx + 1]});
                idx += 2;
            } else {
                adj.add(a, b, Edge{t->elem, values[idx], 0.0});
                idx += 1;
            }
            return;
        }
        if (t->kind == NK::Ser) {
            std::vector<int> chain{a};
            for (size_t k = 0; k + 1 < t->kids.size(); ++k) chain.push_back(counter++);
            chain.push_back(b);
            for (size_t k = 0; k < t->kids.size(); ++k)
                emit(t->kids[k].get(), chain[k], chain[k + 1]);
        } else {  // PAR: every child spans the same terminal pair
            for (const auto& c : t->kids) emit(c.get(), a, b);
        }
    };
    emit(tree.get(), 0, 1);
    return adj;
}

Adjacency candidateToAdjacency(const Candidate& cand) {
    return treeToAdjacency(cand.tree, cand.theta);
}

}  // namespace rlc
