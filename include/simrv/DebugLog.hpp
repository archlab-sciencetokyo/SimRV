#pragma once
#include <fstream>

namespace simrv {
    // Returns a reference to the singleton MMU debug log.
    std::ofstream& get_mmu_log();
}
