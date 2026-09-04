#pragma once
// ASCII report generation (mirrors the Python report format) -- port of
// netgraph_id/report.py.

#include "components.hpp"
#include "selector.hpp"

#include <string>
#include <vector>

namespace ng {

std::string formatReport(const std::string& title,
                         const std::vector<EquivalenceClass>& classes,
                         const ComponentSet& compset,
                         const std::string& truthStr = "", int topK = 8,
                         const std::vector<std::string>& extraLines = {});

}  // namespace ng
