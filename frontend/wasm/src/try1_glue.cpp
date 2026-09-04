// try1_glue.cpp — Try1 bridge (rlc namespace): fully unknown one-port
// identification with an optional exact-device-count prior.
//
//   lcr_try1(f, zre, zim, n, exactN /*0 = free search*/, maxN /*0 = default*/,
//            topK) -> JSON
//
// Candidates are the ranked equivalence classes; the representative's SP tree
// is emitted as the unified adjacency matrix plus a theory curve evaluated
// with rlc::evalThetaFreq on the shared display grid.

#include "adjacency.hpp"
#include "circuits.hpp"
#include "fit_engine_a.hpp"
#include "identify.hpp"
#include "selector.hpp"

#include "common_glue.hpp"

#include <chrono>
#include <complex>
#include <optional>
#include <vector>

namespace {

using lcr_glue::Json;

void emitAdjacencyTry1(Json& j, const rlc::Adjacency& adj) {
    j.key("adjacency");
    j.raw("{");
    j.key("v");
    j.num(adj.V());
    j.raw(",\"slots\":[");
    bool firstSlot = true;
    for (const auto& entry : adj.occupied()) {
        const int u = std::get<0>(entry);
        const int v = std::get<1>(entry);
        const std::vector<rlc::Edge>* edges = std::get<2>(entry);
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
        for (const rlc::Edge& e : *edges) {
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

extern "C" char* lcr_try1(const double* f, const double* zre, const double* zim,
                          int n, int exactN, int maxN, int topK) {
    using namespace rlc;
    try {
        const std::string bad = lcr_glue::checkMeasurements(f, zre, zim, n);
        if (!bad.empty()) return lcr_glue::errorOut("bad_input", bad);

        std::vector<double> fv(f, f + n);
        std::vector<Complex> zv(n);
        for (int i = 0; i < n; ++i) zv[i] = Complex(zre[i], zim[i]);

        Config cfg;
        if (exactN > 0) {
            if (exactN > 12) return lcr_glue::errorOut("bad_input", "exact device count must be <= 12");
            cfg.exactN = exactN;
            if (cfg.maxN < exactN) cfg.maxN = exactN;
        }
        if (maxN > 0) {
            if (maxN > 12) return lcr_glue::errorOut("bad_input", "maxN must be <= 12");
            cfg.maxN = maxN;
        }

        const auto t0 = std::chrono::steady_clock::now();
        IdentifyResult res = identify(fv, zv, nullptr, &cfg);
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        const int k = lcr_glue::clampTopK(topK);
        const std::vector<double> grid = lcr_glue::theoryGrid(fv);

        Json j;
        j.raw("{\"ok\":true,\"try\":1,\"elapsed\":");
        j.num(elapsed);
        j.raw(",\"stats\":{\"n_library\":");
        j.num(res.nLibrary);
        j.raw(",\"n_pruned_kept\":");
        j.num(res.nPrunedKept);
        j.raw(",\"n_classes\":");
        j.num((int)res.classes.size());
        j.raw("},\"candidates\":[");

        const int nOut = std::min<int>(k, (int)res.classes.size());
        for (int i = 0; i < nOut; ++i) {
            const EquivalenceClass& ec = res.classes[i];
            const Candidate& c = ec.representative;
            if (i) j.raw(",");
            j.raw("{");
            j.key("rank");
            j.num(i + 1);
            j.raw(",");
            j.key("devices");
            j.num(nLeaves(c.tree));
            j.raw(",");
            j.key("n_params");
            j.num(c.nParams());
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
            j.num(c.rss);
            j.raw(",");
            j.key("engine");
            j.str(c.engine);
            j.raw(",");
            j.key("n_members");
            j.num(1 + (int)ec.members.size());
            j.raw(",");
            j.key("topology");
            j.str(c.canonicalStr());
            j.raw(",");
            const rlc::Adjacency adj = candidateToAdjacency(c);
            emitAdjacencyTry1(j, adj);
            j.raw(",");
            std::vector<Complex> zt(grid.size());
            evalThetaFreq(c.tree, c.theta, grid.data(), grid.size(), zt.data());
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
        return lcr_glue::errorOut("internal", "unknown error in try1");
    }
}
