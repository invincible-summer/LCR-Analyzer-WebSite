#include "report.hpp"
#include "synthetic.hpp"

#include <cmath>
#include <cstdio>

namespace ng {

namespace {

std::string sci3(double x) {
    if (std::isinf(x)) return "inf";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3e", x);
    return buf;
}

}  // namespace

std::string formatReport(const std::string& title,
                         const std::vector<EquivalenceClass>& classes,
                         const ComponentSet& compset,
                         const std::string& truthStr, int topK,
                         const std::vector<std::string>& extraLines) {
    std::string out = "== " + title + " ==\n";
    out += "components: ";
    const auto& labels = compset.labels();
    for (size_t i = 0; i < labels.size(); ++i) {
        if (i) out += ", ";
        out += labels[i];
    }
    out += "\n";
    if (!truthStr.empty()) out += "truth     : " + truthStr + "\n";
    out += "\n";
    // header mirrors the Python column layout exactly (101 columns)
    out += " rk topology                                                      "
           " V SP     wRMSE     maxRel        RSS #eq\n";
    out += "-----------------------------------------------------------------------------------------------------\n";
    int limit = std::min((int)classes.size(), topK);
    for (int rank = 1; rank <= limit; ++rank) {
        const EquivalenceClass& cl = classes[rank - 1];
        const Candidate& c = cl.representative;
        std::string wiring = networkStr(c.network, compset);
        if (wiring.size() > 58) wiring = wiring.substr(0, 55) + "...";
        std::string note =
            cl.nMembers() > 1 ? "x" + std::to_string(cl.nMembers()) : "";
        char line[256];
        std::snprintf(line, sizeof(line), "%2d %-58s %2d %2c %9s %9s %9s%4s\n", rank,
                      wiring.c_str(), c.network.structure.V, c.sp ? 'Y' : 'N',
                      sci3(c.wrmse).c_str(), sci3(c.maxRelErr).c_str(),
                      sci3(c.rss).c_str(), note.c_str());
        out += line;
    }
    if (!extraLines.empty()) {
        out += "\n";
        for (const auto& s : extraLines) out += s + "\n";
    }
    if (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

}  // namespace ng
