#pragma once

#include <cstdint>

namespace simrv::core {

/// Immutable fast-path decisions captured by a runner at a batch boundary.
struct FastBatchPolicy {
    bool copy_pipeline_context = false;
    bool collect_instruction_mix = false;
    bool poll_pause = true;
    bool has_instruction_limit = false;
    uint32_t pause_poll_mask = 0xFFu;
};

}  // namespace simrv::core
