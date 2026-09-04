// Batch case runner for the py-vs-cpp consistency harness: loads a case
// directory (measurements.txt + topology.txt per ../../../INPUT_FORMAT.md),
// runs fitGraph() and writes the result as JSON (see tools/compare.py).

#include "fit.hpp"
#include "iofmt.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace tf;

namespace {

std::string jnum(double v) {
    char buf[40];
    if (!std::isfinite(v)) {
        std::snprintf(buf, sizeof(buf), "1e999");  // JSON sentinel for inf
        return buf;
    }
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

void writeResult(const std::string& path, const FitResult& r) {
    std::ofstream out(path);
    out << "{\n";
    // structural (exact-match) part: reduction
    out << "  \"n_groups\": " << r.reduction.nGroups() << ",\n";
    out << "  \"n_passes\": " << r.reduction.nPasses << ",\n";
    out << "  \"groups\": [";
    for (size_t g = 0; g < r.reduction.edges.size(); ++g) {
        const auto& e = r.reduction.edges[g];
        if (g) out << ", ";
        out << "[" << e.u << "," << e.v << ",\"" << e.kind << "\",[";
        for (size_t q = 0; q < e.members.size(); ++q) {
            if (q) out << ",";
            out << e.members[q];
        }
        out << "],\"" << (e.expr.leaf ? std::string("e") : (e.expr.tag == 'p' ? std::string("par") : std::string("ser")))
            << "\"]";
    }
    out << "],\n";
    out << "  \"dropped\": [";
    bool first = true;
    for (const auto& [i, why] : r.reduction.dropped) {
        if (!first) out << ",";
        first = false;
        out << "[" << i << ",\"" << why << "\"]";
    }
    out << "],\n";
    // fit quality + diagnostics
    out << "  \"rss\": " << jnum(r.rss) << ", \"wrmse\": " << jnum(r.wrmse)
        << ", \"max_rel\": " << jnum(r.maxRel) << ", \"aicc\": " << jnum(r.aiccVal)
        << ",\n";
    out << "  \"n_params\": " << r.nParams << ", \"n_starts_used\": " << r.nStartsUsed
        << ", \"jac_rank\": " << r.jacRank << ", \"jac_cond\": " << jnum(r.jacCond)
        << ",\n";
    // fitted physical group values
    out << "  \"fit_values\": [";
    for (size_t g = 0; g < r.groups.size(); ++g) {
        const auto& gr = r.groups[g];
        if (g) out << ", ";
        out << "[\"" << gr.kind << "\"," << jnum(gr.value.v1);
        if (gr.kind == 'L') out << "," << jnum(gr.value.v2);
        out << "]";
    }
    out << "],\n";
    int nWeak = 0, nAtBnd = 0;
    for (const auto& gr : r.groups) {
        nWeak += (int)gr.weakParams.size();
        nAtBnd += (int)gr.atBound.size();
    }
    out << "  \"weak\": " << nWeak << ", \"at_bound\": " << nAtBnd << ",\n";
    out << "  \"seconds\": " << jnum(r.seconds) << "\n}\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: case_run <case_dir> <out.json>\n");
        return 2;
    }
    std::string dir = argv[1];
    try {
        Measurements ms = loadMeasurements(dir + "/measurements.txt");
        auto edges = loadTopology(dir + "/topology.txt");
        FitConfig cfg;
        FitResult r = fitGraph(ms.f, ms.z, edges, &cfg);
        writeResult(argv[2], r);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "case_run %s: %s\n", dir.c_str(), e.what());
        std::ofstream out(argv[2]);
        out << "{\"error\": \"" << e.what() << "\"}\n";
        return 1;
    }
}
