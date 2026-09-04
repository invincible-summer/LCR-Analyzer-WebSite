// try2_glue.cpp — Try2 bridge (ng namespace): known component multiset,
// exhaustive topology identification.
//
//   lcr_try2(f, zre, zim, n, kinds[R|L|C], values, dcrs, counts, rows, topK)
//
// `rows` component specs are expanded by `counts` into the ComponentSet
// (canonical order is the lib's business). Candidates are equivalence classes
// of wirings; each representative network is emitted as the unified adjacency
// matrix plus a theory curve via ng::networkZ.

#include "adjacency.hpp"
#include "components.hpp"
#include "graph.hpp"
#include "identify.hpp"
#include "nodal.hpp"
#include "selector.hpp"

#include "common_glue.hpp"

#include <chrono>
#include <complex>
#include <utility>
#include <vector>

namespace {

using lcr_glue::Json;

void emitAdjacencyTry2(Json& j, const ng::Adjacency& adj) {
    j.key("adjacency");
    j.raw("{");
    j.key("v");
    j.num(adj.V());
    j.raw(",\"slots\":[");
    bool firstSlot = true;
    for (const auto& entry : adj.occupied()) {
        const int u = std::get<0>(entry);
        const int v = std::get<1>(entry);
        const std::vector<ng::Edge>* edges = std::get<2>(entry);
        if (!firstSlot) j.raw(",");
        firstSlot = false;
        j.raw("{");
        j.key("u");
        j.num(u);
        j.raw(",");
        j.key("j");
        j.num(v);
        j.raw(",\"edges\":[");
        bool firstEdge = true;
        for (const ng::Edge& e : *edges) {
            if (!firstEdge) j.raw(",");
            firstEdge = false;
            j.raw("{\"t\":\"");
            j.raw(std::string(1, e.type));
            j.raw("\",\"p\":");
            j.num(e.parameter);
            j.raw(",\"d\":");
            j.num(e.dcr);
            j.raw("}");
        }
        j.raw("]}");
    }
    j.raw("]}");
}

}  // namespace

extern "C" char* lcr_try2(const double* f, const double* zre, const double* zim,
                          int n, const char* kinds, const double* values,
                          const double* dcrs, const int* counts, int rows,
                          int topK) {
    using namespace ng;
    try {
        const std::string bad = lcr_glue::checkMeasurements(f, zre, zim, n);
        if (!bad.empty()) return lcr_glue::errorOut("bad_input", bad);
        if (rows < 1 || !kinds || !values || !dcrs || !counts)
            return lcr_glue::errorOut("bad_input", "at least one component row is required");

        std::vector<double> rs;
        std::vector<double> cs;
        std::vector<std::pair<double, double>> ls;
        int total = 0;
        for (int r = 0; r < rows; ++r) {
            const char kind = kinds[r];
            if (kind != 'R' && kind != 'L' && kind != 'C')
                return lcr_glue::errorOut("bad_input",
                          "component row " + std::to_string(r + 1) + ": kind must be R, L or C");
            if (!(values[r] > 0.0) || !std::isfinite(values[r]))
                return lcr_glue::errorOut("bad_input",
                          "component row " + std::to_string(r + 1) + ": value must be > 0");
            if (dcrs[r] < 0.0 || !std::isfinite(dcrs[r]))
                return lcr_glue::errorOut("bad_input",
                          "component row " + std::to_string(r + 1) + ": DCR must be >= 0");
            if (counts[r] < 1 || counts[r] > 64)
                return lcr_glue::errorOut("bad_input",
                          "component row " + std::to_string(r + 1) + ": count must be 1..64");
            if (kind != 'L' && dcrs[r] != 0.0)
                return lcr_glue::errorOut("bad_input",
                          "component row " + std::to_string(r + 1) + ": only L rows may carry a DCR");
            total += counts[r];
            for (int c = 0; c < counts[r]; ++c) {
                switch (kind) {
                    case 'R': rs.push_back(values[r]); break;
                    case 'C': cs.push_back(values[r]); break;
                    default: ls.emplace_back(values[r], dcrs[r]); break;
                }
            }
        }
        if (total > 8)
            return lcr_glue::errorOut("bad_input",
                      "total component count " + std::to_string(total) +
                      " exceeds the exhaustive-search budget (<= 8, recommended <= 6)");

        // must contain at least one storage element (Try2 A4 rule)
        if (cs.empty() && ls.empty())
            return lcr_glue::errorOut("bad_input",
                      "the multiset needs at least one L or C (a resistor-only network is not identifiable)");

        ComponentSet compset = ComponentSet::make(rs, cs, ls);

        std::vector<double> fv(f, f + n);
        std::vector<Complex> zv(n);
        for (int i = 0; i < n; ++i) zv[i] = Complex(zre[i], zim[i]);

        Config cfg;
        cfg.topK = lcr_glue::clampTopK(topK);

        const auto t0 = std::chrono::steady_clock::now();
        IdentifyResult res = identify(compset, fv, zv, nullptr, &cfg);
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        const std::vector<double> grid = lcr_glue::theoryGrid(fv);

        Json j;
        j.raw("{\"ok\":true,\"try\":2,\"elapsed\":");
        j.num(elapsed);
        j.raw(",\"stats\":{\"n_candidates\":");
        j.num((double)res.nCandidates);
        j.raw(",\"n_structures\":");
        j.num(res.nStructures);
        j.raw(",\"n_funnel_kept\":");
        j.num(res.nFunnelKept);
        j.raw(",\"n_components\":");
        j.num(compset.n());
        j.raw(",\"elapsed_engine\":");
        j.num(res.elapsed);
        j.raw("},\"candidates\":[");

        const int nOut = std::min<int>(cfg.topK, (int)res.classes.size());
        for (int i = 0; i < nOut; ++i) {
            const EquivalenceClass& ec = res.classes[i];
            const Candidate& c = ec.representative;
            if (i) j.raw(",");
            j.raw("{");
            j.key("rank");
            j.num(i + 1);
            j.raw(",");
            j.key("devices");
            j.num(compset.n());
            j.raw(",");
            j.key("n_params");
            j.num(compset.nParams());
            j.raw(",");
            j.key("wrmse");
            j.num(ec.wrmse());
            j.raw(",");
            j.key("max_rel");
            j.num(ec.maxRelErr());
            j.raw(",");
            j.key("aicc");
            j.num(ec.aicc());
            j.raw(",");
            j.key("rss");
            j.num(ec.rss());
            j.raw(",");
            j.key("sp");
            j.boolean(c.sp);
            j.raw(",");
            j.key("structure");
            j.str(c.network.structure.serialize());
            j.raw(",");
            j.key("n_members");
            j.num(ec.nMembers());
            j.raw(",");
            const ng::Adjacency adj = candidateToAdjacency(c, compset);
            emitAdjacencyTry2(j, adj);
            j.raw(",");
            const std::vector<Complex> zt = networkZ(c.network, compset, grid);
            lcr_glue::emitComplexArrays(j, grid, zt);
            j.raw("}");
        }
        j.raw("]}");
        return lcr_glue::takeString(j.s);
    } catch (const std::invalid_argument& e) {
        return lcr_glue::errorOut("bad_input", e.what());
    } catch (const std::exception& e) {
        return lcr_glue::errorOut("internal", e.what());
    } catch (...) {
        return lcr_glue::errorOut("internal", "unknown error in try2");
    }
}
