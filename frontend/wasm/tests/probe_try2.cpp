// probe_try2.cpp — evaluate a SPECIFIC structure/mult with given component
// values through the engine's nodal path, for cross-checking against an
// independent evaluator.
// usage: probe_try2 --mult "0,2,0,0,1,1" --comps "C:4.8e-6;C:4.35e-10;C:1e-11;L:1.35e-5:2.6e-4"
//        --f "100,200,500"
#include "components.hpp"
#include "enumerate.hpp"
#include "graph.hpp"
#include "nodal.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace ng;

int main(int argc, char** argv) {
    std::vector<int> mult;
    int V = 0;
    std::vector<Component> comps;
    std::vector<double> freqs;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--mult")) {
            std::string s = argv[++i];
            size_t p = 0;
            while (p < s.size()) {
                size_t q = s.find(',', p);
                if (q == std::string::npos) q = s.size();
                mult.push_back(std::atoi(s.substr(p, q - p).c_str()));
                p = q + 1;
            }
            V = (int)mult.size() == nSlots(2);
            // infer V from slot count
            for (int v = 2; v <= 12; ++v)
                if (nSlots(v) == (int)mult.size()) V = v;
        } else if (!std::strcmp(argv[i], "--comps")) {
            std::string s = argv[++i];
            size_t p = 0;
            while (p < s.size()) {
                size_t q = s.find(';', p);
                if (q == std::string::npos) q = s.size();
                std::string item = s.substr(p, q - p);
                // kind:value[:dcr]
                size_t c1 = item.find(':');
                char kind = item[0];
                double v = std::atof(item.c_str() + c1 + 1);
                double dcr = 0.0;
                size_t c2 = item.find(':', c1 + 1);
                if (c2 != std::string::npos) dcr = std::atof(item.c_str() + c2 + 1);
                comps.push_back(makeComponent(kind, v, dcr));
                p = q + 1;
            }
        } else if (!std::strcmp(argv[i], "--f")) {
            std::string s = argv[++i];
            size_t p = 0;
            while (p < s.size()) {
                size_t q = s.find(',', p);
                if (q == std::string::npos) q = s.size();
                freqs.push_back(std::atof(s.substr(p, q - p).c_str()));
                p = q + 1;
            }
        }
    }
    if (mult.empty() || comps.empty() || freqs.empty()) {
        std::fprintf(stderr, "need --mult, --comps, --f\n");
        return 2;
    }
    Structure st = makeStructure(V, mult, false);
    std::printf("structure key=%s nEdges=%d V=%d\n", st.key().c_str(), st.nEdges(), V);
    // stamps from the raw component vector, instances in slot order mapped to
    // components in the given order
    StructureStamps stamps = StructureStamps::build(st, comps);
    std::vector<int> assign(st.nEdges());
    for (int i = 0; i < st.nEdges(); ++i) assign[i] = i % (int)comps.size();
    for (double f : freqs) {
        Complex s(0.0, 2.0 * M_PI * f);
        auto zc = stamps.zFull({assign}, {s})[0];
        std::printf("f=%10.4g  Z=%14.7g %+.7g j  |Z|=%.7g\n", f, zc[0].real(),
                    zc[0].imag(), std::abs(zc[0]));
    }
    return 0;
}
