// fitbench.cpp — native CLI driver for benchmarking the three engines through
// the exact C ABI the website's worker calls.  Reads a measurement CSV
// (f, Re(Z), Im(Z); '#' comments; tolerates whitespace/semicolons), runs one
// Try, prints the raw JSON response to stdout (Python side parses/scores).
//
// usage:
//   fitbench <csv> try1 [--exact N] [--maxn N] [--topk K] [--seed S]
//   fitbench <csv> try2 --rows "R:200:0:1;C:1e-7:0:1;L:1e-3:2:1" [--topk K]
//   fitbench <csv> try3 --edges "0 1 R;0 1 C" [--topk K]
//
// try2 rows: kind:value[:dcr][:count], dcr only valid on L rows.
// try3 edges: "u v kind" per edge, semicolon separated.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
char* lcr_try1(const double*, const double*, const double*, int, int, int, int);
char* lcr_try2(const double*, const double*, const double*, int, const char*,
               const double*, const double*, const int*, int, int);
char* lcr_try3(const double*, const double*, const double*, int, const int*,
               const int*, const char*, int);
void lcr_free(char*);
}

namespace {

std::vector<std::string> splitAny(const std::string& s, const char* seps) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::strchr(seps, c)) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// tolerant CSV: returns false with a message on structural problems
bool readCsv(const std::string& path, std::vector<double>& f,
             std::vector<double>& re, std::vector<double>& im, std::string& err) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        err = "cannot open " + path;
        return false;
    }
    std::string line;
    int c;
    auto flushLine = [&]() {
        if (line.empty()) return;
        // strip CR
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        std::string t = line;
        line.clear();
        // trim
        size_t a = t.find_first_not_of(" \t");
        if (a == std::string::npos) return;
        size_t b = t.find_last_not_of(" \t");
        t = t.substr(a, b - a + 1);
        if (t[0] == '#') return;
        std::vector<std::string> tok = splitAny(t, ",; \t");
        if (tok.size() != 3) {
            err = "line does not have 3 fields: " + t;
            return;
        }
        char* e1 = nullptr, *e2 = nullptr, *e3 = nullptr;
        double fv = std::strtod(tok[0].c_str(), &e1);
        double rv = std::strtod(tok[1].c_str(), &e2);
        double iv = std::strtod(tok[2].c_str(), &e3);
        if (!e1 || !e2 || !e3 || *e1 || *e2 || *e3) {
            err = "bad number in line: " + t;
            return;
        }
        f.push_back(fv);
        re.push_back(rv);
        im.push_back(iv);
    };
    while ((c = std::fgetc(fp)) != EOF) {
        if (c == '\n') {
            flushLine();
            if (!err.empty()) break;
        } else {
            line += static_cast<char>(c);
        }
    }
    std::fclose(fp);
    flushLine();
    if (err.empty() && f.size() < 4) err = "fewer than 4 data points";
    if (err.empty()) {
        // sort ascending by frequency (engines assume band-ordered data)
        std::vector<size_t> idx(f.size());
        for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
        std::stable_sort(idx.begin(), idx.end(),
                         [&](size_t a, size_t b) { return f[a] < f[b]; });
        std::vector<double> f2, r2, i2;
        for (size_t k : idx) {
            f2.push_back(f[k]);
            r2.push_back(re[k]);
            i2.push_back(im[k]);
        }
        f = std::move(f2);
        re = std::move(r2);
        im = std::move(i2);
    }
    return err.empty();
}

const char* needArg(int argc, char** argv, int& i, const char* what) {
    if (i + 1 >= argc) {
        std::fprintf(stderr, "missing value for %s\n", what);
        std::exit(2);
    }
    return argv[++i];
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: fitbench <csv> try1|try2|try3 [options]\n");
        return 2;
    }
    std::vector<double> f, re, im;
    std::string err;
    if (!readCsv(argv[1], f, re, im, err)) {
        std::fprintf(stderr, "csv error: %s\n", err.c_str());
        return 2;
    }
    const int n = (int)f.size();
    std::string cmd = argv[2];
    char* out = nullptr;

    if (cmd == "try1") {
        int exactN = 0, maxN = 0, topK = 5;
        for (int i = 3; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--exact") exactN = std::atoi(needArg(argc, argv, i, "--exact"));
            else if (a == "--maxn") maxN = std::atoi(needArg(argc, argv, i, "--maxn"));
            else if (a == "--topk") topK = std::atoi(needArg(argc, argv, i, "--topk"));
            else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
        }
        out = lcr_try1(f.data(), re.data(), im.data(), n, exactN, maxN, topK);
    } else if (cmd == "try2") {
        int topK = 5;
        std::string rows;
        for (int i = 3; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--rows") rows = needArg(argc, argv, i, "--rows");
            else if (a == "--topk") topK = std::atoi(needArg(argc, argv, i, "--topk"));
            else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
        }
        std::vector<std::string> rowList = splitAny(rows, ";");
        std::string kinds;
        std::vector<double> vals, dcrs;
        std::vector<int> counts;
        for (const auto& r : rowList) {
            std::vector<std::string> fld = splitAny(r, ",:");
            if (fld.empty() || fld[0].size() != 1) {
                std::fprintf(stderr, "bad row %s\n", r.c_str());
                return 2;
            }
            kinds += fld[0][0];
            vals.push_back(fld.size() > 1 ? std::strtod(fld[1].c_str(), nullptr) : 0.0);
            dcrs.push_back(fld.size() > 2 ? std::strtod(fld[2].c_str(), nullptr) : 0.0);
            counts.push_back(fld.size() > 3 ? std::atoi(fld[3].c_str()) : 1);
        }
        out = lcr_try2(f.data(), re.data(), im.data(), n, kinds.c_str(),
                       vals.data(), dcrs.data(), counts.data(), (int)rowList.size(), topK);
    } else if (cmd == "try3") {
        int topK = 5;
        std::string edges;
        for (int i = 3; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--edges") edges = needArg(argc, argv, i, "--edges");
            else if (a == "--topk") topK = std::atoi(needArg(argc, argv, i, "--topk"));
            else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
        }
        std::vector<std::string> edgeList = splitAny(edges, ";");
        std::vector<int> us, vs;
        std::string kinds;
        for (const auto& e : edgeList) {
            std::vector<std::string> fld = splitAny(e, ",: ");
            if (fld.size() != 3) {
                std::fprintf(stderr, "bad edge %s\n", e.c_str());
                return 2;
            }
            us.push_back(std::atoi(fld[0].c_str()));
            vs.push_back(std::atoi(fld[1].c_str()));
            kinds += fld[2][0];
        }
        out = lcr_try3(f.data(), re.data(), im.data(), n, us.data(), vs.data(),
                       kinds.c_str(), (int)edgeList.size());
    } else {
        std::fprintf(stderr, "unknown command %s\n", cmd.c_str());
        return 2;
    }

    if (!out) {
        std::fprintf(stderr, "null result\n");
        return 1;
    }
    std::fputs(out, stdout);
    std::fputc('\n', stdout);
    lcr_free(out);
    return 0;
}
