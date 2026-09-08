#include "lcr/lcr.hpp"
#include <fstream>
#include <iostream>
#include <random>
using namespace lcr;
int main(int argc, char **argv) {
  try {
    std::string mode = argc > 1 ? argv[1] : "random";
    Config c;
    c.starts = 20;
    c.iterations = 200;
    c.topK = 8;
    if (mode == "real4") {
      std::string root = argc > 2 ? argv[2] : "../examples";
      std::vector<Graph> truths = {
          {2, {{0, 1, {'R', 9900, 0}}, {0, 1, {'C', 1e-7, 0}}}},
          {2, {{0, 1, {'L', .1, 345}}, {0, 1, {'C', 1e-6, 0}}}},
          {3,
           {{0, 2, {'L', .1, 345}},
            {0, 2, {'C', 1e-7, 0}},
            {2, 1, {'C', 1e-6, 0}}}},
          {3,
           {{0, 2, {'R', 200, 0}},
            {0, 2, {'L', 820e-6, 13}},
            {0, 2, {'C', 1e-6, 0}},
            {2, 1, {'R', 50, 0}}}}};
      double limits[] = {.010, .021, .023, .006};
      bool ok = true;
      for (int i = 0; i < 4; ++i) {
        std::ifstream f(root + "/data" + std::to_string(i + 1) + ".csv");
        if (!f)
          throw std::runtime_error("missing real data");
        auto d = loadCsv(f);
        auto g = truths[i];
        auto r3 = try3(d, g, c);
        std::vector<Edge> es;
        for (auto e : g.edges)
          es.push_back(e.element);
        auto r2 = try2(d, es, c);
        Config tolerance = c;
        tolerance.tolerance = .5;
        auto rt = try2(d, es, tolerance);
        Config search = c;
        search.maxN = 4;
        auto r1 = try1(d, search);
        double w3 =
                   r3.candidates.empty() ? inf : r3.candidates[0].metrics.wrmse,
               wt =
                   rt.candidates.empty() ? inf : rt.candidates[0].metrics.wrmse,
               w1 =
                   r1.candidates.empty() ? inf : r1.candidates[0].metrics.wrmse;
        std::cout << "data" << i + 1 << " try1=" << w1 << " exact="
                  << (r2.candidates.empty() ? inf
                                            : r2.candidates[0].metrics.wrmse)
                  << " tolerance=" << wt << " try3=" << w3
                  << " seconds=" << r1.elapsed << std::endl;
        ok = ok && w3 < limits[i] && wt < limits[i] && w1 < limits[i] * 1.5;
      }
      return ok ? 0 : 1;
    }
    if (mode != "random" && mode != "case")
      throw std::invalid_argument("mode must be real4, random, or case");
    int only = mode == "case" ? (argc > 2 ? std::stoi(argv[2]) : 0) : -1;
    int n = only >= 0 ? only + 1 : (argc > 2 ? std::stoi(argv[2]) : 40);
    if (n < 1 || n > 100000)
      throw std::invalid_argument("invalid benchmark count");
    unsigned seed = argc > 3 ? std::stoul(argv[3]) : 21;
    std::mt19937 rng(seed);
    std::normal_distribution<double> normal;
    int pass1 = 0, pass8 = 0, structural = 0;
    double seconds = 0;
    for (int i = 0; i < n; ++i) {
      auto lib = spLibrary(2 + i % 3, 4);
      Graph g = lib[rng() % lib.size()];
      for (auto &b : g.edges) {
        char t = b.element.type;
        b.element.parameter = (t == 'R'   ? 100.
                               : t == 'L' ? .001
                                          : 1e-6) *
                              std::pow(10, normal(rng));
        b.element.parameterOfCapacitanceDCResistance =
            t == 'L' ? (i % 5 == 0 ? 0 : std::pow(10, normal(rng))) : 0;
      }
      Data clean, d;
      double noise = (i % 4) * .001, smooth = (i % 3) * .003;
      int points = i % 2 ? 20 : 60;
      for (int j = 0; j < points; ++j) {
        double u = double(j) / (points - 1), f = 10 * std::pow(1e4, u);
        auto z = forward(g, f, false);
        if (z.status != SolveStatus::OK)
          continue;
        clean.push_back({f, z.z});
        Complex value =
            z.z * (1. + Complex(noise * normal(rng), noise * normal(rng)) +
                   smooth * std::sin(2 * pi * u));
        if (i % 7 == 0 && j == points / 3)
          value *= 1.5;
        d.push_back({f, value});
      }
      if (only >= 0 && i != only)
        continue;
      Config cfg = c;
      cfg.seed = seed + i;
      cfg.starts = 12;
      cfg.robust = i % 7 == 0;
      cfg.maxN = 4;
      auto r = try1(d, cfg);
      seconds += r.elapsed;
      if (only >= 0) {
        std::cout << "truth:\n";
        printAdjacency(std::cout, g);
        std::cout << "measurements:\n";
        dumpMeasurements(std::cout, d);
        report(std::cout, r, d, cfg);
      }

      bool found = false;
      double gate = std::max(.003, 4 * (noise + smooth));
      for (size_t k = 0; k < r.candidates.size(); ++k) {
        auto a = r.candidates[k];
        std::vector<Complex> z;
        for (auto p : clean)
          z.push_back(forward(a.graph, p.f, false).z);
        auto cleanError = metrics(clean, z, 0, c).wrmse;
        if (only >= 0)
          std::cout << "rank=" << k + 1 << " clean_wrmse=" << cleanError
                    << " gate=" << gate << '\n';
        if (cleanError < gate) {
          found = true;
          if (k == 0)
            ++pass1;
        }
        if (k == 0 && canonical(a.graph, false) == canonical(g, false))
          ++structural;
      }
      if (found)
        ++pass8;
      std::cout << "case=" << i << " behavior=" << found
                << " elapsed=" << r.elapsed << std::endl;
    }
    std::cout << "seed=" << seed << " n=" << (only >= 0 ? 1 : n)
              << " pass@1=" << pass1 << " pass@8=" << pass8
              << " structure@1=" << structural << " seconds=" << seconds
              << '\n';
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
