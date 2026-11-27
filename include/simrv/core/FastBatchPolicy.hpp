#pragma once

#include <cstdint>

namespace simrv::core {

enum class HpmClassMode : uint8_t {
    Disabled = 0,
    ListenersOnly = 1,
    Dynamic = 2,
};

/// Structural compile-time descriptor for specializing IA fast execution kernels.
struct FastKernelSpec {
    bool copy_pipeline_context = false;
    bool collect_instruction_mix = false;
    bool poll_pause = true;
    HpmClassMode hpm_mode = HpmClassMode::Disabled;

    friend auto operator==(const FastKernelSpec&, const FastKernelSpec&) -> bool = default;
};

/// Immutable fast-path decisions captured by a runner at a batch boundary.
struct FastBatchPolicy {
    bool copy_pipeline_context = false;
    bool collect_instruction_mix = false;
    bool poll_pause = true;
    bool has_instruction_limit = false;
};

}  // namespace simrv::core
