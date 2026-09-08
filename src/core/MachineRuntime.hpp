/**
 * @file MachineRuntime.hpp
 * @brief Internal runtime state and runner declarations for Machine.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <thread>
#include <variant>
#include <vector>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/debug/BreakpointManager.hpp"
#include "simrv/debug/GdbStub.hpp"
#include "simrv/debug/SpikeLockstep.hpp"
#include "simrv/debug/SymbolTable.hpp"
#include "simrv/device/AIA.hpp"
#include "simrv/device/Aclint.hpp"
#include "simrv/device/Power.hpp"
#include "simrv/device/Rtc.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/device/mmio/VirtioMmioBlock.hpp"
#include "simrv/device/mmio/VirtioMmioConsole.hpp"
#include "simrv/device/mmio/VirtioMmioGpu.hpp"
#include "simrv/device/mmio/VirtioMmioInput.hpp"
#include "simrv/device/mmio/VirtioMmioNet.hpp"
#include "simrv/device/mmio/VirtioMmioRng.hpp"
#include "simrv/device/mmio/VirtioMmioSound.hpp"
#include "simrv/device/pci/PcieRootComplex.hpp"
#include "simrv/device/pci/VirtioPciBlock.hpp"
#include "simrv/device/pci/VirtioPciConsole.hpp"
#include "simrv/device/pci/VirtioPciGpu.hpp"
#include "simrv/device/pci/VirtioPciInput.hpp"
#include "simrv/device/pci/VirtioPciNet.hpp"
#include "simrv/device/pci/VirtioPciRng.hpp"
#include "simrv/device/pci/VirtioPciSound.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

class RunnerBase {
   public:
    virtual ~RunnerBase();
    void wait_for_quiescence();
    void stop(Machine& machine);

   protected:
    void start_threads(Machine& machine, bool baremetal);
    static void reset_runner_transients(Machine& machine);
    static void execute_ca_batch(Machine& machine);
    static void execute_instruction_smp(Machine& machine, bool baremetal);
    void stop_threads();

    std::vector<std::jthread> worker_threads_;
    std::atomic<bool> workers_running_{false};
    std::atomic<uint32_t> workers_in_cycle_{0};
    Machine* machine_ = nullptr;
};

/// Bare-metal and OS scheduling stay outside CPU's per-instruction fast path.
class BaremetalRunner : public RunnerBase {
   public:
    void start(Machine& machine);
    void prepare(Machine& machine);
    void execute(Machine& machine);
    [[nodiscard]] auto execute_fast_batch(Machine& machine, uint32_t batch_size) -> bool;
    void finalize(Machine& machine);
};

class OsRunner : public RunnerBase {
   public:
    void start(Machine& machine);
    void prepare(Machine& machine);
    void execute(Machine& machine);
    [[nodiscard]] auto execute_fast_batch(Machine& machine, uint32_t batch_size) -> bool;
    void finalize(Machine& machine);
};

/// Owns zero-initialized DRAM without eagerly touching every host page.
class RamStorage {
   public:
    RamStorage() = default;
    ~RamStorage() { reset(); }
    RamStorage(const RamStorage&) = delete;
    auto operator=(const RamStorage&) -> RamStorage& = delete;
    RamStorage(RamStorage&&) noexcept = default;
    auto operator=(RamStorage&&) noexcept -> RamStorage& = default;

    [[nodiscard]] auto allocate(size_t bytes) -> bool {
        if (bytes == 0) return false;
        auto* replacement = static_cast<Byte*>(std::calloc(bytes, sizeof(Byte)));
        if (replacement == nullptr) return false;
        reset();
        data_ = replacement;
        owned_ = true;
        return true;
    }

    void reset() noexcept {
        if (owned_) {
            std::free(data_);
            owned_ = false;
        }
        data_ = nullptr;
    }

    [[nodiscard]] auto data() const noexcept -> Byte* { return data_; }
    [[nodiscard]] auto mutable_data_ref() noexcept -> Byte*& { return data_; }

   private:
    Byte* data_ = nullptr;
    bool owned_ = false;
};

class Machine::Runtime {
   public:
    using ExecutionRunner = std::variant<BaremetalRunner, OsRunner>;

    explicit Runtime(Machine& machine, bool appmode);

    CPU primary_cpu;
    std::vector<std::unique_ptr<CPU>> secondary_harts;
    RamStorage ram;
    std::unique_ptr<simrv::Rtc> rtc;
    std::unique_ptr<simrv::device::Uart> uart;
    std::unique_ptr<simrv::device::PowerMmio> power;
    std::unique_ptr<simrv::device::AclintMtimer> aclint_mtimer;
    std::unique_ptr<simrv::device::AclintMswi> aclint_mswi;
    std::unique_ptr<simrv::device::Imsic> imsic_m;
    std::unique_ptr<simrv::device::Imsic> imsic_s;
    std::unique_ptr<simrv::device::Aplic> aplic_m;
    std::unique_ptr<simrv::device::Aplic> aplic_s;
    std::unique_ptr<simrv::device::PcieRootComplex> pcie;
    std::shared_ptr<simrv::device::VirtioPciBlock> pci_disk;
    std::shared_ptr<simrv::device::VirtioPciConsole> pci_console;
    std::shared_ptr<simrv::device::VirtioPciRng> pci_rng;
    std::shared_ptr<simrv::device::VirtioPciGpu> pci_gpu;
    std::shared_ptr<simrv::device::VirtioPciInput> pci_input;
    std::shared_ptr<simrv::device::VirtioPciSound> pci_sound;
    std::shared_ptr<simrv::device::VirtioPciNet> pci_net;
    std::shared_ptr<simrv::device::VirtioMmioBlock> mmio_disk;
    std::shared_ptr<simrv::device::VirtioMmioConsole> mmio_console;
    std::shared_ptr<simrv::device::VirtioMmioRng> mmio_rng;
    std::shared_ptr<simrv::device::VirtioMmioGpu> mmio_gpu;
    std::shared_ptr<simrv::device::VirtioMmioInput> mmio_input;
    std::shared_ptr<simrv::device::VirtioMmioSound> mmio_sound;
    std::shared_ptr<simrv::device::VirtioMmioNet> mmio_net;
    std::unique_ptr<simrv::debug::GdbStub> gdb_stub;
    std::unique_ptr<simrv::debug::SpikeLockstep> spike_lockstep;
    simrv::debug::BreakpointManager breakpoints;
    Tracer tracer;
    simrv::debug::SymbolTable symbols;
    simrv::memory::MemorySubsystem memory;
    ExecutionRunner runner;
};

}  // namespace simrv::core
