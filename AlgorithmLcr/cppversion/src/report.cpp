#include "report.hpp"

#include <cmath>
#include <cstdio>

namespace rlc {

namespace {
std::string fmt(double x, int width = 10, int prec = 4) {
    if (!std::isfinite(x)) {
        std::string s = x > 0 ? "inf" : "-inf";
        while ((int)s.size() < width) s = " " + s;
        return s;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%*.*g", width, prec, x);
    return buf;
}
}  // namespace

std::string formatReport(const std::string& title,
                         const std::vector<EquivalenceClass>& classes,
                         const std::string* truth, int topK,
                         const std::vector<std::string>* extraLines) {
    std::vector<std::string> lines;
    std::string bar(96, '=');
    std::string dash(96, '-');
    lines.push_back(bar);
    lines.push_back("DUT: " + title);
    if (truth) lines.push_back("truth: " + *truth);
    lines.push_back(dash);
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%3s %-46s %2s %10s %10s %10s  note", "rk",
                      "topology", "n", "wRMSE", "maxRelErr", "AICc");
        lines.push_back(buf);
    }
    lines.push_back(dash);
    if (classes.empty()) lines.push_back("(no valid candidates)");
    double aicc0 = classes.empty() ? 0.0 : classes[0].aicc();
    int rank = 1;
    for (const auto& eq : classes) {
        if (rank > topK) break;
        const Candidate& rep = eq.representative;
        std::string topo = toString(rep.tree, &rep.theta);
        if (topo.size() > 46) topo = topo.substr(0, 43) + "...";
        double daicc = rep.aiccVal - aicc0;
        std::vector<std::string> notes;
        if (eq.members.size() > 1) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "equiv x%zu", eq.members.size());
            notes.push_back(buf);
        }
        if (daicc > 0 && daicc < 2.0) notes.push_back("dAICc<2");
        if (rep.engine == "B") notes.push_back("engB");
        if (!rep.note.empty()) notes.push_back(rep.note);
        std::string noteJoin;
        for (size_t i = 0; i < notes.size(); ++i) {
            if (i) noteJoin += "; ";
            noteJoin += notes[i];
        }
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%3d %-46s %2d %s %s %s  %s", rank, topo.c_str(),
                      rep.nParams(), fmt(rep.wrmse).c_str(), fmt(rep.maxRelErr).c_str(),
                      fmt(rep.aiccVal, 10, 2).c_str(), noteJoin.c_str());
        lines.push_back(buf);
        int altCount = 0;
        for (const auto& member : eq.members) {
            if (&member == &rep) continue;
            if (altCount >= 2) break;
            ++altCount;
            std::string mtopo = toString(member.tree, &member.theta);
            if (mtopo.size() > 40) mtopo = mtopo.substr(0, 37) + "...";
            char b2[128];
            std::snprintf(b2, sizeof(b2), "    + equiv: %-40s (eng%s, AICc %.2f)",
                          mtopo.c_str(), member.engine.c_str(), member.aiccVal);
            lines.push_back(b2);
        }
        ++rank;
    }
    if (extraLines) {
        lines.push_back(dash);
        for (const auto& l : *extraLines) lines.push_back(l);
    }
    lines.push_back(bar);
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += "\n";
        out += lines[i];
    }
    return out;
}

}  // namespace rlc
