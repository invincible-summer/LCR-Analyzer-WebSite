// probe_try1.cpp — stage-by-stage diagnostic for the Try1 identify() pipeline.
// Prints asymptotic features, the engine-B rational model table, the F3
// conservative energy bound, which library trees survive pruning, and the
// per-tree engine-A fit quality.  This is a debugging tool: it mirrors
// identify()'s steps with public APIs only.
//
// usage: probe_try1 <csv> [--exact N] [--maxn N] [--filter SUBSTR]

#include "circuits.hpp"
#include "fit_engine_a.hpp"
#include "fit_engine_b.hpp"
#include "identify.hpp"
#include "library.hpp"
#include "pruning.hpp"
#include "selector.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace rlc;
using C = std::complex<double>;

static bool readCsv(const std::string& path, std::vector<double>& f,
                    std::vector<double>& re, std::vector<double>& im) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    std::string line;
    int c;
    auto flush = [&]() {
        if (line.empty()) return;
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        std::string t = line;
        line.clear();
        if (!t.empty() && t[0] == '#') return;
        std::vector<double> v;
        size_t a = 0;
        while (a < t.size()) {
            size_t b = t.find_first_of(",; \t", a);
            if (b == std::string::npos) b = t.size();
            if (b > a) v.push_back(std::atof(t.substr(a, b - a).c_str()));
            a = b + 1;
        }
        if (v.size() == 3) {
            f.push_back(v[0]);
            re.push_back(v[1]);
            im.push_back(v[2]);
        }
    };
    while ((c = std::fgetc(fp)) != EOF) {
        if (c == '\n') flush();
        else line += static_cast<char>(c);
    }
    std::fclose(fp);
    flush();
    std::vector<size_t> idx(f.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(),
                     [&](size_t a, size_t b) { return f[a] < f[b]; });
    auto apply = [&](std::vector<double>& v) {
        std::vector<double> o;
        for (size_t k : idx) o.push_back(v[k]);
        v = std::move(o);
    };
    apply(f);
    apply(re);
    apply(im);
    return f.size() >= 4;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: probe_try1 <csv> [--exact N] [--maxn N] [--filter SUB]\n");
        return 2;
    }
    std::vector<double> f, re, im;
    if (!readCsv(argv[1], f, re, im)) {
        std::fprintf(stderr, "csv error\n");
        return 2;
    }
    int exactN = 0, maxN = 0;
    const char* filter = nullptr;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--exact")) exactN = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--maxn")) maxN = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--filter")) filter = argv[++i];
    }
    const size_t m = f.size();
    std::vector<C> z(m);
    for (size_t k = 0; k < m; ++k) z[k] = C(re[k], im[k]);
    std::vector<C> s(m);
    std::vector<double> w(m);
    for (size_t k = 0; k < m; ++k) {
        w[k] = 2.0 * M_PI * f[k];
        s[k] = C(0.0, 1.0) * w[k];
    }
    std::vector<double> wts = defaultWeights(z);

    // 0. features
    AsymptoticFeatures feat = extractAsymptotics(w, z);
    std::printf("features: slopeLow=%.3f slopeHigh=%.3f phaseLow=%.2f phaseHigh=%.2f\n",
                feat.slopeLow, feat.slopeHigh, feat.phaseLowDeg, feat.phaseHighDeg);
    std::printf("          lEst=%.4g cEst=%.4g rLevel=%.4g hasWRes=%d wRes=%.4g\n",
                feat.lEst, feat.cEst, feat.rLevel, (int)feat.hasWRes, feat.wRes);

    // 1. engine B rational table
    FosterResult foster = fosterCandidates(w, z, wts, 4, 15);
    auto dumpModel = [&](const char* name, const RationalModel& chosen) {
        std::printf("%s order selection (nObs=2m=%zu):\n", name, 2 * m);
        std::printf("  %2s %4s %12s %12s %12s\n", "n", "p", "rss", "rss/dof", "aicc");
        for (const auto& mm : chosen.alternatives) {
            double dof = std::max(2.0 * (double)m - (double)mm.nUnknowns, 1.0);
            std::printf("  %2d %4d %12.4g %12.4g %12.1f %s\n", mm.order, mm.nUnknowns,
                        mm.rss, mm.rss / dof, mm.selAicc,
                        mm.order == chosen.order ? "<== selected" : "");
        }
        int degree, nPair, nReal;
        chosen.poleStructure(s, degree, nPair, nReal);
        std::printf("  selected structure: degree=%d nPair=%d nReal=%d\n", degree, nPair, nReal);
    };
    dumpModel("zModel", foster.zModel);
    dumpModel("yModel", foster.yModel);
    for (const auto& fc : foster.candidates) {
        std::printf("foster %s cand: skipped=%d wrmse=%.4g %s\n", fc.engine.c_str(),
                    (int)fc.skipped, fc.wrmse, fc.note.c_str());
    }
    int minEnergy = conservativeEnergyBound(foster.zModel, s);
    std::printf("minEnergy (F3 bound) = %d\n", minEnergy);

    // 2. library + pruning
    Config cfg;
    std::vector<TreePtr> libStore;
    const std::vector<TreePtr>* lib;
    if (exactN > 0) {
        cfg.exactN = exactN;
        libStore = TopologyLibrary::ofSize(exactN, cfg.maxIDepth);
        lib = &libStore;
    } else {
        if (maxN > 0) cfg.maxN = maxN;
        lib = &TopologyLibrary::get(cfg.maxN, cfg.maxIDepth);
    }
    std::vector<TreePtr> kept = pruneTrees(*lib, feat, minEnergy, cfg.enableF2, cfg.enableF3);
    std::printf("library=%zu kept=%zu\n", lib->size(), kept.size());
    std::printf("--- pruned trees ---\n");
    for (const auto& t : *lib) {
        std::string cs = canonical(t);
        if (std::any_of(kept.begin(), kept.end(),
                        [&](const TreePtr& k) { return canonical(k) == cs; }))
            continue;
        if (!filter || cs.find(filter) != std::string::npos)
            std::printf("  PRUNED %s\n", cs.c_str());
    }

    // 3. engine A over kept trees (default funnel)
    std::map<std::string, std::vector<std::vector<double>>> extraStarts;
    for (const auto& cand : foster.candidates)
        if (!cand.skipped) extraStarts[cand.canonicalStr()].push_back(cand.theta);
    StartHints hints = hintsFromFeatures(feat);
    std::vector<Candidate> fits = fitLibrary(kept, s, z, wts, cfg.engineAConfig(),
                                             &hints, &extraStarts);
    std::sort(fits.begin(), fits.end(),
              [](const Candidate& a, const Candidate& b) { return a.rss < b.rss; });
    std::printf("--- engine A fits (kept trees, RSS order) ---\n");
    for (const auto& c : fits) {
        std::string cs = canonical(c.tree);
        if (filter && cs.find(filter) == std::string::npos) continue;
        std::printf("  wRMSE=%-.4g max=%.3g p=%2d rss=%.4g %s\n", c.wrmse, c.maxRelErr,
                    c.nParams(), c.rss, cs.c_str());
    }
    return 0;
}
