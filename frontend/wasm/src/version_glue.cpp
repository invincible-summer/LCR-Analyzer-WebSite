// version_glue.cpp — tiny TU with no engine dependency: module identity +
// the shared free() for every JSON string returned by the lcr_tryX entries.

#include "common_glue.hpp"

extern "C" {

const char* lcr_version() {
    return "lcr-wasm 1.0.0 (Try1 rlc / Try2 ng / Try3 tf — AlgorithmLcr cppversion)";
}

void lcr_free(char* p) {
    if (p) std::free(p);
}

}  // extern "C"
