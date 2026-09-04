// IO suite: unified text formats (measurements + topology) and the
// adjacency-matrix output placement -- mirrors test_iofmt.py/test_adjacency.py.

#include "framework.hpp"
#include "adjacency.hpp"
#include "fit.hpp"
#include "iofmt.hpp"
#include "synthetic.hpp"

using namespace tf;

TEST(io_measurements_roundtrip_bitexact) {
    std::vector<double> f{10.0, 1234.5678, 9.87654321e6};
    std::vector<Complex> z{{1.5, -2.5}, {3.25e-7, 0.0}, {-1e-13, 6.02e23}};
    std::string text = formatMeasurements(f, z);
    Measurements ms = parseMeasurements(text);
    CHECK(ms.f.size() == 3);
    for (int k = 0; k < 3; ++k) {
        CHECK(ms.f[k] == f[k]);
        CHECK(ms.z[k] == z[k]);
    }
}

TEST(io_topology_roundtrip_and_examples) {
    // INPUT_FORMAT.md example 1: ladder V=3
    std::string ex1 = "3\n0 1\n2\nL\nC\nR\n";
    auto e1 = parseTopology(ex1);
    CHECK(e1.size() == 3);
    CHECK(e1[0] == std::make_tuple(0, 2, 'L'));
    CHECK(e1[1] == std::make_tuple(1, 2, 'C'));
    CHECK(e1[2] == std::make_tuple(1, 2, 'R'));
    std::string back = formatTopology(e1);
    auto e1b = parseTopology(back);
    CHECK(e1 == e1b);
    // example 2: triple parallel on the port
    std::string ex2 = "2\n3\nR\nC\nL\n";
    auto e2 = parseTopology(ex2);
    CHECK(e2.size() == 3);
    CHECK(e2[0] == std::make_tuple(0, 1, 'R'));
    CHECK(e2[2] == std::make_tuple(0, 1, 'L'));
}

TEST(io_topology_validation) {
    bool threw = false;
    try { parseTopology("1\n"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);  // V must be >= 2
    threw = false;
    try { parseTopology("3\n0 1\n2\nL\nC\n"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);  // queue needs E=3, got 2
    threw = false;
    try { parseTopology("3\n0 1\n2 3\nL\nC\nR\n"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);  // row 1 needs V-1-1=1 integer
}

TEST(adjacency_placement_and_notes) {
    // nested_red keeps original node labels: groups on 0-4 and 4-1, node 5
    // survives empty after the dangling C is dropped
    auto duts = makeDuts();
    const DUT* nested = nullptr;
    for (const auto& d : duts)
        if (d.name == "nested_red") nested = &d;
    auto [f, z] = measure(*nested, nullptr, 0.0, 0);
    FitConfig cfg;
    FitResult r = fitGraph(f, z, nested->edges, &cfg);
    Adjacency adj = fitresultToAdjacency(r);
    CHECK(adj.V() == 6);  // original labels 0/1/4/5 + span
    CHECK(adj.nEdges() == r.reduction.nGroups());
    int occupied5 = 0;
    for (const auto& [i, j, edges] : adj.occupied()) {
        (void)edges;
        if (i == 5 || j == 5) ++occupied5;
    }
    CHECK(occupied5 == 0);  // node 5's C edge was dropped
    auto notes = adjacencyNotes(r);
    bool hasDrop = false;
    for (const auto& s : notes)
        if (s.find("dropped") != std::string::npos) hasDrop = true;
    CHECK(hasDrop);
}

TEST(adjacency_z_cross_validation) {
    // rebuild the edge table from the matrix and evaluate with an
    // independent stamping: the fitted network's Z must match zModel
    auto duts = makeDuts();
    const DUT* dut = nullptr;
    for (const auto& d : duts)
        if (d.name == "ladder") dut = &d;
    auto [f, z] = measure(*dut, nullptr, 0.0, 0);
    FitResult r = fitGraph(f, z, dut->edges, nullptr);
    std::vector<Complex> zm = r.zModel(f);
    Adjacency adj = fitresultToAdjacency(r);
    int V = adj.V();
    for (size_t k = 0; k < f.size(); k += 7) {
        Complex s(0.0, 2.0 * 3.14159265358979 * f[k]);
        std::vector<Complex> Y((size_t)(V - 1) * (V - 1), {0.0, 0.0});
        for (const auto& [i, j, edges] : adj.occupied()) {
            Complex ysum(0.0, 0.0);
            for (const Edge& e : *edges) {
                ysum += e.type == 'R' ? Complex(1.0 / e.parameter, 0.0)
                      : e.type == 'C' ? s * e.parameter
                      : 1.0 / (Complex(e.dcr, 0.0) + s * e.parameter);
            }
            int ri = i == 0 ? -1 : i - 1, rj = j == 0 ? -1 : j - 1;
            if (ri >= 0) Y[(size_t)ri * (V - 1) + ri] += ysum;
            if (rj >= 0) Y[(size_t)rj * (V - 1) + rj] += ysum;
            if (ri >= 0 && rj >= 0) {
                Y[(size_t)ri * (V - 1) + rj] -= ysum;
                Y[(size_t)rj * (V - 1) + ri] -= ysum;
            }
        }
        int n = V - 1;
        std::vector<Complex> A = Y, b((size_t)n, {0.0, 0.0});
        b[0] = {1.0, 0.0};
        luSolveComplex(A, n, b);
        double rel = std::abs(b[0] - zm[k]) / std::abs(zm[k]);
        CHECK(rel < 1e-8);
    }
}
