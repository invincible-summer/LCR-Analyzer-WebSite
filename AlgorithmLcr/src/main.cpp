#include "lcr/lcr.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
namespace {
std::ifstream open(const std::string &path) {
  std::ifstream f(path);
  if (!f)
    throw std::invalid_argument("cannot open " + path);
  return f;
}
double real(const std::string &s) {
  size_t n;
  double x = std::stod(s, &n);
  if (n != s.size() || !std::isfinite(x))
    throw std::invalid_argument("invalid number " + s);
  return x;
}
int integer(const std::string &s) {
  double x = real(s);
  if (x < 0 || x > 1000000000 || x != int(x))
    throw std::invalid_argument("invalid integer " + s);
  return int(x);
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc < 2 || std::string(argv[1]) == "--help") {
      std::cout << "lcr try1|try2|try3 --measurements FILE [--count "
                   "FILE|--exact-n N] [--components FILE|--topology FILE]\n  "
                   "--csv FILE (alternative measurement adapter) --max-n 4 "
                   "--max-depth 4 --top-k 8\n  --mode strict|fast --starts 16 "
                   "--iterations 160 --seed 1 --budget N --seconds S\n  "
                   "--tolerance FRACTION --dcr-tolerance OHMS (Try2 only) "
                   "--robust --json\n  --r-min/--r-max --l-min/--l-max "
                   "--c-min/--c-max --dcr-max SI_VALUE\n";
      return 0;
    }
    std::string which = argv[1];
    if (which != "try1" && which != "try2" && which != "try3")
      throw std::invalid_argument("unknown engine");
    lcr::Config c;
    std::map<std::string, std::string> args;
    bool json = false;
    for (int i = 2; i < argc; ++i) {
      std::string k = argv[i];
      if (k == "--json") {
        json = true;
        continue;
      }
      if (k == "--robust") {
        c.robust = true;
        continue;
      }
      if (i + 1 == argc || args.count(k))
        throw std::invalid_argument("missing/duplicate option " + k);
      args[k] = argv[++i];
    }
    const std::string allowed =
        " --measurements --csv --count --exact-n --components --topology "
        "--max-n --max-depth --top-k --mode --starts --iterations --seed "
        "--budget --seconds --tolerance --dcr-tolerance --r-min --r-max "
        "--l-min --l-max --c-min --c-max --dcr-max ";
    for (auto &kv : args)
      if (allowed.find(" " + kv.first + " ") == std::string::npos)
        throw std::invalid_argument("unknown option " + kv.first);
    if (args.count("--measurements") + args.count("--csv") != 1)
      throw std::invalid_argument(
          "provide exactly one of --measurements/--csv");
    for (auto kv : {std::pair<const char *, int *>{"--max-n", &c.maxN},
                    {"--max-depth", &c.maxDepth},
                    {"--top-k", &c.topK},
                    {"--starts", &c.starts},
                    {"--iterations", &c.iterations}})
      if (args.count(kv.first))
        *kv.second = integer(args[kv.first]);
    for (auto kv : {std::pair<const char *, double *>{"--seconds", &c.seconds},
                    {"--tolerance", &c.tolerance},
                    {"--dcr-tolerance", &c.dcrAbsoluteTolerance},
                    {"--r-min", &c.rMin},
                    {"--r-max", &c.rMax},
                    {"--l-min", &c.lMin},
                    {"--l-max", &c.lMax},
                    {"--c-min", &c.cMin},
                    {"--c-max", &c.cMax},
                    {"--dcr-max", &c.dcrMax}})
      if (args.count(kv.first))
        *kv.second = real(args[kv.first]);
    if (args.count("--seed"))
      c.seed = integer(args["--seed"]);
    if (args.count("--budget"))
      c.candidateBudget = integer(args["--budget"]);
    if (args.count("--mode")) {
      auto m = args["--mode"];
      if (m != "strict" && m != "fast")
        throw std::invalid_argument("mode must be strict/fast");
      c.mode = m == "strict" ? lcr::Config::Strict : lcr::Config::Fast;
    }
    if (which != "try1" && (args.count("--count") || args.count("--exact-n") ||
                            args.count("--max-n") || args.count("--max-depth")))
      throw std::invalid_argument("count/SP bounds are Try1 only");
    if (which != "try2" &&
        (args.count("--components") || args.count("--tolerance") ||
         args.count("--dcr-tolerance")))
      throw std::invalid_argument("components/tolerance are Try2 only");
    if (which != "try3" && args.count("--topology"))
      throw std::invalid_argument("topology is Try3 only");
    if (args.count("--count") && args.count("--exact-n"))
      throw std::invalid_argument("count and exact-n are mutually exclusive");
    if (args.count("--count")) {
      auto f = open(args["--count"]);
      c.exactN = lcr::loadCount(f);
    }
    if (args.count("--exact-n"))
      c.exactN = integer(args["--exact-n"]);
    auto file =
        open(args.count("--csv") ? args["--csv"] : args["--measurements"]);
    auto data =
        args.count("--csv") ? lcr::loadCsv(file) : lcr::loadMeasurements(file);
    lcr::SearchResult result;
    if (which == "try1")
      result = lcr::try1(data, c);
    else if (which == "try2") {
      if (!args.count("--components"))
        throw std::invalid_argument("missing --components");
      auto f = open(args["--components"]);
      result = lcr::try2(data, lcr::loadComponents(f), c);
    } else {
      if (!args.count("--topology"))
        throw std::invalid_argument("missing --topology");
      auto f = open(args["--topology"]);
      result = lcr::try3(data, lcr::loadTopology(f), c);
    }
    lcr::report(std::cout, result, data, c, json);
    return result.candidates.empty() ? 3 : result.enumerationComplete ? 0 : 2;
  } catch (const std::exception &e) {
    std::cerr << "lcr: " << e.what() << '\n';
    return 1;
  }
}
