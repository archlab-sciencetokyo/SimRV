/**
 * @file DirtyDiffEngine.hpp
 * @brief High-performance cell delta diffing and minimal ANSI escape emitter.
 */
#pragma once

#include <cstddef>
#include <string>

#include "simrv/tui/framework/ScreenBuffer.hpp"

namespace simrv::tui::framework {

struct DiffStats {
    std::size_t cells_examined{0};
    std::size_t cells_dirty{0};
    std::size_t runs_emitted{0};
    std::size_t bytes_emitted{0};
    std::size_t rows_dirty{0};
    std::size_t rows_clean{0};
};

class DirtyDiffEngine {
   public:
    DirtyDiffEngine() = default;

    auto emit_delta(const ScreenBuffer& current, const ScreenBuffer& previous, std::string& out,
                    bool force_redraw = false) -> DiffStats;

   private:
    bool truecolor_supported_{terminal_supports_truecolor()};
};

}  // namespace simrv::tui::framework
