#include "adjacency.hpp"

#include <cstdio>
#include <stdexcept>

namespace ng {

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
    rows_.resize((size_t)V);
    for (int i = 0; i < V; ++i) rows_[i].resize((size_t)V - 1 - i);
}

std::vector<Edge>& Adjacency::slot(int i, int j) {
    if (!(0 <= i && i < j && j < V_))
        throw std::invalid_argument("slot outside upper triangle");
    return rows_[i][(size_t)(j - i - 1)];
}

const std::vector<Edge>& Adjacency::slot(int i, int j) const {
    return const_cast<Adjacency*>(this)->slot(i, j);
}

void Adjacency::add(int i, int j, Edge edge) {
    if (i == j) throw std::invalid_argument("self loops are not part of the format");
    if (i > j) std::swap(i, j);
    slot(i, j).push_back(edge);
}

int Adjacency::nEdges() const {
    int n = 0;
    for (const auto& row : rows_)
        for (const auto& cell : row) n += (int)cell.size();
    return n;
}

std::vector<std::tuple<int, int, const std::vector<Edge>*>> Adjacency::occupied() const {
    std::vector<std::tuple<int, int, const std::vector<Edge>*>> out;
    for (int i = 0; i < V_; ++i)
        for (int k = 0; k < V_ - 1 - i; ++k)
            if (!rows_[i][(size_t)k].empty())
                out.push_back({i, i + k + 1, &rows_[i][(size_t)k]});
    return out;
}

std::string Adjacency::formatBlock(const std::string& label,
                                   const std::vector<std::string>& extraLines) const {
    std::string out;
    out += label.empty() ? "adjacency " : ("adjacency[" + label + "] ");
    out += "V=" + std::to_string(V_) + " (ports 0,1):\n";
    for (const auto& [i, j, edges] : occupied()) {
        char head[32];
        std::snprintf(head, sizeof(head), "  (%d,%d): ", i, j);
        out += head;
        for (size_t q = 0; q < edges->size(); ++q) {
            if (q) out += " | ";
            out += fmtEdge((*edges)[q]);
        }
        out += "\n";
    }
    for (const auto& s : extraLines) out += "  " + s + "\n";
    if (!out.empty()) out.pop_back();  // trailing newline
    return out;
}

Adjacency networkToAdjacency(const Network& network, const ComponentSet& compset) {
    return networkToAdjacency(network, compset.components());
}

Adjacency networkToAdjacency(const Network& network, const std::vector<Component>& comps) {
    const int V = network.structure.V;
    const auto& slots = slotList(V);
    const auto soi = network.structure.slotOfInstances();
    Adjacency adj(V);
    for (size_t t = 0; t < network.assign.size(); ++t) {
        auto [i, j] = slots[soi[t]];
        const Component& c = comps[network.assign[t]];
        adj.add(i, j, Edge{c.kind, c.value, c.dcr});
    }
    return adj;
}

Adjacency candidateToAdjacency(const Candidate& cand, const ComponentSet& compset) {
    return networkToAdjacency(cand.network, compset);
}

Adjacency candidateToAdjacency(const Candidate& cand, const std::vector<Component>& comps) {
    return networkToAdjacency(cand.network, comps);
}

}  // namespace ng
