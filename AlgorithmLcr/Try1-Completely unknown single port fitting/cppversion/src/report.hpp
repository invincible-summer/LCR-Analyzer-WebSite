#pragma once
// ASCII identification report — port of rlc_id/report.py.

#include "selector.hpp"

#include <string>
#include <vector>

namespace rlc {

std::string formatReport(const std::string& title,
                         const std::vector<EquivalenceClass>& classes,
                         const std::string* truth = nullptr, int topK = 5,
                         const std::vector<std::string>* extraLines = nullptr);

}  // namespace rlc
