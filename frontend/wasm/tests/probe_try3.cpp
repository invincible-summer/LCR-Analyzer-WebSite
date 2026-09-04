// probe_try3.cpp — experiment driver for Try3 fitGraph with FitConfig knobs.
// usage: probe_try3 <csv> --edges "u v K;..." [--nstarts N] [--ncenter N]
//        [--nperturb N] [--esc W] [--rounds N] [--seed S]
#include "fit.hpp"
#include "metric.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: probe_try3 <csv> --edges \"u v K;...\" [opts]\n");
        return 2;
    }
    std::vector<double> f, re, im;
    std::ifstream in(argv[1]);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line[0] == '#') continue;
        std::stringstream ss(line);
        std::string tok;
        std::vector<double> v;
        while (std::getline(ss, tok, ',')) v.push_back(std::atof(tok.c_str()));
        if (v.size() == 3) {
            f.push_back(v[0]);
            re.push_back(v[1]);
            im.push_back(v[2]);
        }
    }
    std::vector<int> us, vs;
    std::vector<char> ks;
    tf::FitConfig cfg;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--edges")) {
            std::string e = argv[++i];
            size_t p = 0;
            while (p < e.size()) {
                size_t q = e.find(';', p);
                if (q == std::string::npos) q = e.size();
                int u, v2;
                char k;
                if (std::sscanf(e.c_str() + p, "%d %d %c", &u, &v2, &k) == 3) {
                    us.push_back(u);
                    vs.push_back(v2);
                    ks.push_back(k);
                }
                p = q + 1;
            }
        } else if (!std::strcmp(argv[i], "--nstarts")) cfg.nStarts = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--ncenter")) cfg.nCenter = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--nperturb")) cfg.nPerturb = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--npolish")) cfg.nPolish = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--nfevf")) cfg.maxNfevFactor = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--tolc")) cfg.tolCoarse = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--esc")) cfg.escalationWrmse = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--rounds")) cfg.escalationRounds = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--lastbatch")) cfg.lastResortBatch = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--seed")) cfg.seed = std::strtoull(argv[++i], nullptr, 10);
    }
    const size_t n = f.size();
    std::vector<tf::Complex> z(n);
    for (size_t k = 0; k < n; ++k) z[k] = tf::Complex(re[k], im[k]);
    std::vector<std::tuple<int, int, char>> edges;
    for (size_t k = 0; k < us.size(); ++k) edges.emplace_back(us[k], vs[k], ks[k]);

    auto t0 = std::chrono::steady_clock::now();
    tf::FitResult res = tf::fitGraph(f, z, edges, &cfg);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("wrmse=%.6g maxrel=%.4g starts=%d secs=%.2f\n", res.wrmse, res.maxRel,
                res.nStartsUsed, secs);
    for (const auto& g : res.groups) {
        std::printf("  g%d %c %d-%d %s v=%.6g dcr=%.6g weak=%zu bound=%zu\n", g.gid, g.kind,
                    g.u, g.v, g.mode.c_str(), g.value.v1, g.value.v2, g.weakParams.size(),
                    g.atBound.size());
    }
    return 0;
}
