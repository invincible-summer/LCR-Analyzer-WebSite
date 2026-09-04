// Standalone case runner for the Try1 py-vs-cpp consistency harness.
// It links against the EXISTING cppversion library (no source changes):
//   g++ -std=c++17 -O2 -I src tools/case_run1.cpp -L build -lrlc_id \
//       -lpthread -o build/case_run1
// Loads <case_dir>/measurements.txt (+ optional count.txt), runs identify
// with the config mirrored by tools/run_py1.py, writes result JSON.

#include "identify.hpp"
#include "iofmt.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sys/stat.h>
#include <string>
#include <vector>

using namespace rlc;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: case_run1 <case_dir> <out.json>\n");
        return 2;
    }
    std::string dir = argv[1];
    try {
        Measurements ms = loadMeasurements(dir + "/measurements.txt");
        Config cfg;
        cfg.maxN = 5;
        std::string countPath = dir + "/count.txt";
        struct stat st;
        if (stat(countPath.c_str(), &st) == 0) cfg.exactN = loadCount(countPath);
        IdentifyResult res = identify(ms.f, ms.z, nullptr, &cfg);
        std::ofstream out(argv[2]);
        out << "{\n  \"classes\": [\n";
        int limit = std::min((int)res.classes.size(), 5);
        for (int r = 0; r < limit; ++r) {
            const Candidate& c = res.classes[r].representative;
            out << "    {\"canonical\": \"" << c.canonicalStr() << "\", \"theta\": [";
            for (size_t i = 0; i < c.theta.size(); ++i) {
                char b[40];
                std::snprintf(b, sizeof(b), "%.12g", c.theta[i]);
                if (i) out << ", ";
                out << b;
            }
            out << "], \"wrmse\": ";
            char b[40];
            std::snprintf(b, sizeof(b), "%.6g", c.wrmse);
            out << b << ", \"rss\": ";
            std::snprintf(b, sizeof(b), "%.6g", c.rss);
            out << b << ", \"aicc\": ";
            std::snprintf(b, sizeof(b), "%.6g", c.aiccVal);
            out << b << ", \"n_members\": "
                << 1 + (int)res.classes[r].members.size() << "}";
            if (r + 1 < limit) out << ",";
            out << "\n";
        }
        out << "  ]\n}\n";
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "case_run1 %s: %s\n", dir.c_str(), e.what());
        std::ofstream out(argv[2]);
        out << "{\"error\": \"" << e.what() << "\"}\n";
        return 1;
    }
}
