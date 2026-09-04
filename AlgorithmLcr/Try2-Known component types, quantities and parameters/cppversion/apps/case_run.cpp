// Batch case runner for the py-vs-cpp consistency harness: loads a case
// directory (measurements.txt + components.txt per ../../../INPUT_FORMAT.md),
// runs identify() and writes the result as JSON (see tools/compare.py).

#include "adjacency.hpp"
#include "identify.hpp"
#include "iofmt.hpp"
#include "selector.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace ng;

namespace {

std::string jnum(double v) {
    char buf[40];
    if (!std::isfinite(v)) {
        std::snprintf(buf, sizeof(buf), "%s", std::isnan(v) ? "null" : "1e999");
        // JSON has no inf/nan; encode huge sentinel (comparator special-cases)
        std::snprintf(buf, sizeof(buf), "1e999");
        return buf;
    }
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

void writeResult(const std::string& path, const IdentifyResult& res) {
    std::ofstream out(path);
    const auto& comps = res.compset.components();
    out << "{\n";
    out << "  \"n_structures\": " << res.nStructures << ",\n";
    out << "  \"n_candidates\": " << res.nCandidates << ",\n";
    out << "  \"n_funnel_kept\": " << res.nFunnelKept << ",\n";
    auto dumpSerial = [&](const std::vector<std::vector<Component>>& serial) {
        out << "[";
        for (size_t k = 0; k < serial.size(); ++k) {
            if (k) out << ", ";
            out << "[";
            for (size_t q = 0; q < serial[k].size(); ++q) {
                if (q) out << ", ";
                out << "[\"" << serial[k][q].kind << "\", "
                    << jnum(serial[k][q].value) << ", " << jnum(serial[k][q].dcr) << "]";
            }
            out << "]";
        }
        out << "]";
    };
    out << "  \"classes\": [\n";
    int limit = std::min((int)res.classes.size(), 8);
    for (int r = 0; r < limit; ++r) {
        const EquivalenceClass& cl = res.classes[r];
        const Candidate& c = cl.representative;
        out << "    {\"V\": " << c.network.structure.V << ", \"serial\": ";
        dumpSerial(c.network.serialize(comps));
        out << ", \"members_serial\": [";
        for (size_t m = 0; m < cl.members.size(); ++m) {
            if (m) out << ", ";
            dumpSerial(cl.members[m].network.serialize(comps));
        }
        out << "]";
        out << ", \"rss\": " << jnum(c.rss) << ", \"wrmse\": " << jnum(c.wrmse)
            << ", \"max_rel\": " << jnum(c.maxRelErr) << ", \"sp\": "
            << (c.sp ? "true" : "false") << ", \"n_members\": " << cl.nMembers()
            << ", \"adjacency\": ";
        std::string adj = candidateToAdjacency(c, res.compset).formatBlock(std::to_string(r + 1));
        std::string esc;
        for (char ch : adj) {
            if (ch == '\n') esc += "\\n";
            else if (ch == '"') esc += "\\\"";
            else if (ch == '\\') esc += "\\\\";
            else esc += ch;
        }
        out << "\"" << esc.c_str() << "\"}";
        if (r + 1 < limit) out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";
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
        ComponentSet cs = loadComponents(dir + "/components.txt");
        Config cfg;
        IdentifyResult res = identify(cs, ms.f, ms.z, nullptr, &cfg);
        writeResult(argv[2], res);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "case_run %s: %s\n", dir.c_str(), e.what());
        std::ofstream out(argv[2]);
        out << "{\"error\": \"" << e.what() << "\"}\n";
        return 1;
    }
}
