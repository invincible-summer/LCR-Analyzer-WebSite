// IO suite: unified text formats (measurements + components), the
// adjacency-matrix output shape/conservation and its electrical equivalence
// -- mirrors tests/test_iofmt.py and tests/test_adjacency.py.

#include "framework.hpp"
#include "adjacency.hpp"
#include "graph.hpp"
#include "iofmt.hpp"
#include "nodal.hpp"
#include "synthetic.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>

using namespace ng;

TEST(iofmt_measurements_roundtrip_bitexact) {
    std::vector<double> f{10.0, 1234.5678, 9.87654321e6};
    std::vector<std::complex<double>> z{{1.5, -2.5}, {3.25e-7, 0.0}, {-1e-13, 6.02e23}};
    std::string text = formatMeasurements(f, z);
    Measurements ms = parseMeasurements(text);
    CHECK(ms.f.size() == 3);
    for (int k = 0; k < 3; ++k) {
        CHECK(ms.f[k] == f[k]);            // bit-for-bit
        CHECK(ms.z[k] == z[k]);
    }
}

TEST(iofmt_measurements_comments_and_blanks) {
    std::string text =
        "# a comment\n2\n\n1.0e3 9.98e2 -1.2e-1  # trailing\n1.0e4 6.13e2 -4.88e2\n";
    Measurements ms = parseMeasurements(text);
    CHECK(ms.f.size() == 2);
    CHECK_NEAR(ms.f[1], 1e4, 1e-15);
}

TEST(iofmt_measurements_validation) {
    bool threw = false;
    try { parseMeasurements("3\n1 2 3\n4 5 6\n"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);  // n=3 but 2 data lines
    threw = false;
    try { parseMeasurements("2\n0 1 2\n1 2 3\n"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);  // f must be > 0
    threw = false;
    try { parseMeasurements("2\n1 2\n1 2 3\n"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);  // wrong field count
    threw = false;
    try { parseMeasurements(""); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

TEST(iofmt_components_roundtrip_and_order) {
    ComponentSet cs = ComponentSet::make({1e3, 100.0}, {100e-9}, {{1e-3, 5.0}, {10e-6, 0.0}});
    std::string text = formatComponents(cs);
    ComponentSet back = parseComponents(text);
    CHECK(back.n() == cs.n());
    for (int i = 0; i < cs.n(); ++i) {
        CHECK(back.components()[i].kind == cs.components()[i].kind);
        CHECK(back.components()[i].value == cs.components()[i].value);
        CHECK(back.components()[i].dcr == cs.components()[i].dcr);
    }
    // canonical order: 'C' < 'L' < 'R' by kind letter (py tuple-of-str order)
    CHECK(cs.components()[0].kind == 'C');
    CHECK(cs.components()[1].kind == 'L');
    CHECK(cs.components()[2].kind == 'L');
    CHECK(cs.components()[3].kind == 'R');
}

TEST(iofmt_components_validation) {
    bool threw = false;
    try { parseComponents("X 1.0\n"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { parseComponents("R 1.0 2.0\n"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);  // R must have exactly 2 fields
    threw = false;
    try { parseComponents("C -1.0\n"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);  // value must be positive
    threw = false;
    try { parseComponents("L 1.0 -2.0\n"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);  // dcr must be >= 0
    threw = false;
    try { parseComponents(""); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    // L with 2 or 3 fields is fine
    parseComponents("L 1.0\nL 1.0 2.0\n");
}

TEST(adjacency_shape_and_conservation) {
    auto duts = makeDuts();
    for (const auto& dut : duts) {
        Adjacency adj = networkToAdjacency(dut.network, dut.compset);
        CHECK(adj.nEdges() == dut.compset.n());
        // row i has V-1-i cells (checked via occupied bounds)
        for (const auto& [i, j, edges] : adj.occupied()) {
            CHECK(0 <= i && i < j && j < adj.V());
            CHECK(!edges->empty());
        }
        // slot access enforces i < j
        bool threw = false;
        try { adj.slot(1, 0); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
    }
}

TEST(adjacency_z_cross_validation) {
    // Rebuild the edge table from the matrix and evaluate Z with an
    // independent stamping -- proves the expansion is electrically exact.
    auto duts = makeDuts();
    std::vector<double> f{1e2, 1e4, 1e6};
    for (const auto& dut : duts) {
        Adjacency adj = networkToAdjacency(dut.network, dut.compset);
        int V = adj.V();
        std::vector<std::complex<double>> z = networkZ(dut.network, dut.compset, f);
        for (size_t k = 0; k < f.size(); ++k) {
            std::complex<double> s(0.0, 2.0 * 3.14159265358979323846 * f[k]);
            std::vector<std::complex<double>> Y((size_t)(V - 1) * (V - 1), {0.0, 0.0});
            for (const auto& [i, j, edges] : adj.occupied()) {
                std::complex<double> ysum(0.0, 0.0);
                for (const Edge& e : *edges) {
                    ysum += e.type == 'R' ? std::complex<double>(1.0 / e.parameter, 0.0)
                          : e.type == 'C' ? s * e.parameter
                          : 1.0 / (std::complex<double>(e.dcr, 0.0) + s * e.parameter);
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
            std::vector<std::complex<double>> A = Y, b((size_t)n, {0.0, 0.0});
            b[0] = {1.0, 0.0};
            luSolveComplex(A, n, b);
            double rel = std::abs(b[0] - z[k]) / std::abs(z[k]);
            CHECK(rel < 1e-10);
        }
    }
}

TEST(adjacency_format_block) {
    ComponentSet cs = ComponentSet::make({1e3}, {100e-9}, {});
    Network net = networkFromEdges(cs, {{0, 1, 0}, {0, 1, 1}});
    std::string blk = networkToAdjacency(net, cs).formatBlock("1");
    CHECK(blk.find("adjacency[1] V=2 (ports 0,1):") == 0);
    // in-slot edge order = instance order; canonical component order sorts
    // 'C' before 'R' (kind-letter ordering shared with the py reference)
    CHECK(blk.find("(0,1): C 1.000e-07 | R 1.000e+03") != std::string::npos);
}
