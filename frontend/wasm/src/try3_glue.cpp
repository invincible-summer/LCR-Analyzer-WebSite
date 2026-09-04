// try3_glue.cpp — Try3 bridge (tf namespace): known topology + per-edge
// component kinds, continuous parameter inversion.
//
//   lcr_try3(f, zre, zim, n, us, vs, kinds[R|L|C], m) -> JSON
//
// Single deterministic result: fitted group values placed back on the
// original node labels (unified adjacency), full diagnostics (weak params,
// at-bound flags, Jacobian rank/condition, merged/dropped edge notes).

#include "adjacency.hpp"
#include "fit.hpp"
#include "graph.hpp"
#include "nodal.hpp"

#include "common_glue.hpp"

#include <chrono>
#include <complex>
#include <tuple>
#include <vector>

namespace {

using lcr_glue::Json;

void emitAdjacencyTry3(Json& j, const tf::Adjacency& adj) {
    j.key("adjacency");
    j.raw("{");
    j.key("v");
    j.num(adj.V());
    j.raw(",\"slots\":[");
    bool firstSlot = true;
    for (const auto& entry : adj.occupied()) {
        const int u = std::get<0>(entry);
        const int v = std::get<1>(entry);
        const std::vector<tf::Edge>* edges = std::get<2>(entry);
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
        for (const tf::Edge& e : *edges) {
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

extern "C" char* lcr_try3(const double* f, const double* zre, const double* zim,
                          int n, const int* us, const int* vs, const char* kinds,
                          int m) {
    using namespace tf;
    try {
        const std::string bad = lcr_glue::checkMeasurements(f, zre, zim, n);
        if (!bad.empty()) return lcr_glue::errorOut("bad_input", bad);
        if (m < 1 || !us || !vs || !kinds)
            return lcr_glue::errorOut("bad_input", "at least one edge is required");
        if (m > 32)
            return lcr_glue::errorOut("bad_input", "edge count must be <= 32");

        bool hasPort0 = false, hasPort1 = false;
        int maxLabel = 1;
        std::vector<std::tuple<int, int, char>> edges;
        edges.reserve(m);
        for (int e = 0; e < m; ++e) {
            int u = us[e], v = vs[e];
            const char kind = kinds[e];
            if (u == v || u < 0 || v < 0)
                return lcr_glue::errorOut("bad_input",
                          "edge " + std::to_string(e + 1) + ": need two distinct node labels >= 0");
            if (kind != 'R' && kind != 'L' && kind != 'C')
                return lcr_glue::errorOut("bad_input",
                          "edge " + std::to_string(e + 1) + ": kind must be R, L or C");
            if (u > v) std::swap(u, v);
            hasPort0 |= (u == 0 || v == 0);
            hasPort1 |= (u == 1 || v == 1);
            maxLabel = std::max(maxLabel, v);
            edges.emplace_back(u, v, kind);
        }
        if (!hasPort0 || !hasPort1)
            return lcr_glue::errorOut("bad_input",
                      "nodes 0 and 1 are the port terminals; both must appear in the edge set");
        if (maxLabel >= 16)
            return lcr_glue::errorOut("bad_input", "node labels must stay below 16");

        std::vector<double> fv(f, f + n);
        std::vector<Complex> zv(n);
        for (int i = 0; i < n; ++i) zv[i] = Complex(zre[i], zim[i]);

        const auto t0 = std::chrono::steady_clock::now();
        FitResult r = fitGraph(fv, zv, edges, nullptr);
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        const std::vector<double> grid = lcr_glue::theoryGrid(fv);

        Json j;
        j.raw("{\"ok\":true,\"try\":3,\"elapsed\":");
        j.num(elapsed);
        j.raw(",\"stats\":{\"n_groups\":");
        j.num(r.reduction.nGroups());
        j.raw(",\"n_starts_used\":");
        j.num(r.nStartsUsed);
        j.raw(",\"seconds\":");
        j.num(r.seconds);
        j.raw("},\"candidates\":[");

        // deterministic single result — rank 1
        j.raw("{");
        j.key("rank");
        j.num(1);
        j.raw(",");
        j.key("devices");
        j.num(r.reduction.nGroups());
        j.raw(",");
        j.key("n_params");
        j.num(r.nParams);
        j.raw(",");
        j.key("wrmse");
        j.num(r.wrmse);
        j.raw(",");
        j.key("max_rel");
        j.num(r.maxRel);
        j.raw(",");
        j.key("aicc");
        j.num(r.aiccVal);
        j.raw(",");
        j.key("rss");
        j.num(r.rss);
        j.raw(",");
        const tf::Adjacency adj = fitresultToAdjacency(r);
        emitAdjacencyTry3(j, adj);
        j.raw(",");
        const std::vector<Complex> zt = r.zModel(grid);
        lcr_glue::emitComplexArrays(j, grid, zt);
        j.raw("}");

        j.raw("],\"try3\":{");
        j.key("ok");
        j.boolean(r.ok);
        j.raw(",\"jac_rank\":");
        j.num(r.jacRank);
        j.raw(",\"jac_cond\":");
        j.num(r.jacCond);
        j.raw(",\"n_passes\":");
        j.num(r.reduction.nPasses);
        j.raw(",\"groups\":[");
        for (size_t g = 0; g < r.groups.size(); ++g) {
            const GroupReport& gr = r.groups[g];
            if (g) j.raw(",");
            j.raw("{");
            j.key("gid");
            j.num(gr.gid);
            j.raw(",");
            j.key("kind");
            j.raw("\"");
            j.raw(std::string(1, gr.kind));
            j.raw("\"");
            j.raw(",");
            j.key("u");
            j.num(gr.u);
            j.raw(",");
            j.key("v");
            j.num(gr.v);
            j.raw(",\"members\":[");
            for (size_t i = 0; i < gr.members.size(); ++i) {
                if (i) j.raw(",");
                j.num(gr.members[i]);
            }
            j.raw("],");
            j.key("mode");
            j.str(gr.mode);
            j.raw(",\"value\":{");
            j.key("v1");
            j.num(gr.value.v1);
            j.raw(",");
            j.key("v2");
            j.num(gr.value.v2);
            j.raw("},\"weak\":[");
            for (size_t i = 0; i < gr.weakParams.size(); ++i) {
                if (i) j.raw(",");
                j.str(gr.weakParams[i]);
            }
            j.raw("],\"at_bound\":[");
            for (size_t i = 0; i < gr.atBound.size(); ++i) {
                if (i) j.raw(",");
                j.str(gr.atBound[i]);
            }
            j.raw("]}");
        }
        j.raw("],\"edges\":[");
        for (size_t e = 0; e < r.edgesOut.size(); ++e) {
            const EdgeReport& er = r.edgesOut[e];
            if (e) j.raw(",");
            j.raw("{");
            j.key("index");
            j.num(er.index);
            j.raw(",");
            j.key("kind");
            j.raw("\"");
            j.raw(std::string(1, er.kind));
            j.raw("\"");
            j.raw(",");
            j.key("status");
            j.str(er.status);
            j.raw(",");
            j.key("group");
            j.num(er.group);
            j.raw(",");
            j.key("note");
            j.str(er.note);
            j.raw("}");
        }
        j.raw("],\"notes\":[");
        const std::vector<std::string> notes = adjacencyNotes(r);
        for (size_t i = 0; i < notes.size(); ++i) {
            if (i) j.raw(",");
            j.str(notes[i]);
        }
        j.raw("]}}");
        return lcr_glue::takeString(j.s);
    } catch (const PortOpenError& e) {
        return lcr_glue::errorOut("port_open", e.what());
    } catch (const std::invalid_argument& e) {
        return lcr_glue::errorOut("bad_input", e.what());
    } catch (const std::exception& e) {
        return lcr_glue::errorOut("internal", e.what());
    } catch (...) {
        return lcr_glue::errorOut("internal", "unknown error in try3");
    }
}
