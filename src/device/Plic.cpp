/**
 * @file Plic.cpp
 * @brief Platform-Level Interrupt Controller (PLIC) MMIO device implementation.
 */
#include "simrv/device/Plic.hpp"

#include <cstdint>
#include <ranges>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::device {

namespace {

[[nodiscard]] inline auto find_highest_pending_plic_source(const Plic& plic,
                                                           std::size_t ctx) noexcept -> int {
    if (ctx >= Plic::kMaxPlicContexts) return 0;
    Word max_prio = 0;
    int claim_id = 0;
    for (int i : std::views::iota(1, 32)) {
        const auto idx = static_cast<std::size_t>(i);
        if ((plic.plic_pending[0] & (1u << i)) != 0 &&
            (plic.plic_enables[ctx][0] & (1u << i)) != 0) {
            if (plic.plic_priorities[idx] > max_prio) {
                max_prio = plic.plic_priorities[idx];
                claim_id = i;
            }
        }
    }
    return (max_prio > plic.plic_threshold[ctx]) ? claim_id : 0;
}

}  // namespace

void Plic::update_mip() {
    if (cpu_.machine_ != nullptr) {
        const size_t n_harts = cpu_.machine_->num_harts();
        for (size_t h = 0; h < n_harts; ++h) {
            const bool m_asserted = (find_highest_pending_plic_source(*this, 2 * h) != 0);
            const bool s_asserted = (find_highest_pending_plic_source(*this, 2 * h + 1) != 0);
            cpu_.machine_->set_hart_irq(static_cast<HartId>(h), core::InterruptType::External,
                                        PrivilegeLevel::Machine, m_asserted);
            cpu_.machine_->set_hart_irq(static_cast<HartId>(h), core::InterruptType::External,
                                        PrivilegeLevel::Supervisor, s_asserted);
        }
        return;
    }

    // Standalone CPU without Machine container (e.g. focused unit tests)
    auto& target_state = cpu_.state();
    if (find_highest_pending_plic_source(*this, 0) != 0) {
        target_state.mip |= enum_mask(core::MipBit::Meip);
    } else {
        target_state.mip &= ~enum_mask(core::MipBit::Meip);
    }
    target_state.seip_external = (find_highest_pending_plic_source(*this, 1) != 0);
    target_state.refresh_supervisor_pending();
}

void Plic::set_irq(IrqNumber irq_num, int state_val) {
    if (irq_num == 0 || irq_num >= 32) return;
    const Word mask = static_cast<Word>(1) << irq_num;
    if (state_val != 0) {
        plic_pending[0] |= mask;
    } else {
        plic_pending[0] &= ~mask;
    }
    update_mip();
}

void Plic::reset() {
    plic_pending.fill(0);
    plic_priorities.fill(0);
    for (auto& enables : plic_enables) enables.fill(0);
    plic_threshold.fill(0);
    plic_claim.fill(0);
}

auto Plic::handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool {
    if (req.opcode == memory::TlOpcodeA::Get) {
        resp.data = mmio_read(offset(req.address.raw()));
    } else {
        mmio_write(offset(req.address.raw()), req.data);
    }
    return true;
}

auto Plic::get_context_for_offset(Address offset) const -> std::optional<PlicContextId> {
    if (offset >= 0x200000 && offset < 0x200000 + (kMaxPlicContexts * 0x1000)) {
        return static_cast<PlicContextId>((offset - 0x200000) / 0x1000);
    }
    return std::nullopt;
}

auto Plic::mmio_read(Address offset) -> Word {
    if (offset < 0x1000) {
        // PLIC interrupt source zero is reserved and its priority is hardwired to zero.
        if (offset == 0) return 0;
        return plic_priorities[offset / 4];
    }
    if (offset >= 0x1000 && offset < 0x1080) {
        return plic_pending[(offset - 0x1000) / 4];
    }
    if (offset >= 0x2000 && offset < 0x2000 + (kMaxPlicContexts * 0x80)) {
        const auto ctx = (offset - 0x2000) / 0x80;
        const auto word = ((offset - 0x2000) % 0x80) / 4;
        const Word value = plic_enables[ctx][word];
        return (word == 0) ? (value & ~Word{1}) : value;
    }

    if (const auto context = get_context_for_offset(offset)) {
        const auto ctx_idx = static_cast<std::size_t>(*context);
        Address ctx_base = 0x200000 + (*context * 0x1000);
        if (offset == ctx_base) {
            return plic_threshold[ctx_idx];
        }
        if (offset == ctx_base + 4) {
            const int claim_id = find_highest_pending_plic_source(*this, ctx_idx);
            if (claim_id > 0) {
                plic_pending[0] &= ~(1u << claim_id);
                plic_claim[ctx_idx] = static_cast<InterruptSourceId>(claim_id);
                update_mip();
            }
            return static_cast<Word>(claim_id);
        }
    }
    return 0;
}

void Plic::mmio_write(Address offset, Word wdata) {
    if (offset < 0x1000) {
        // Source zero means "no interrupt" and is not configurable.
        if (offset != 0) plic_priorities[offset / 4] = static_cast<InterruptPriority>(wdata);
    } else if (offset >= 0x2000 && offset < 0x2000 + (kMaxPlicContexts * 0x80)) {
        const auto ctx = (offset - 0x2000) / 0x80;
        const auto word = ((offset - 0x2000) % 0x80) / 4;
        plic_enables[ctx][word] = (word == 0) ? (wdata & ~Word{1}) : wdata;
    } else {
        if (const auto context = get_context_for_offset(offset)) {
            const auto ctx_idx = static_cast<std::size_t>(*context);
            Address ctx_base = 0x200000 + (*context * 0x1000);
            if (offset == ctx_base) {
                plic_threshold[ctx_idx] = static_cast<InterruptPriority>(wdata);
            } else if (offset == ctx_base + 4) {
                // Complete: indicates the handler has finished with the IRQ
                if (plic_claim[ctx_idx] == static_cast<InterruptSourceId>(wdata) && wdata != 0) {
                    plic_claim[ctx_idx] = 0;
                    if (cpu_.machine_ && wdata == 3 && cpu_.machine_->uart_device() &&
                        cpu_.machine_->uart_device()->is_interrupt_pending()) {
                        set_irq(static_cast<IrqNumber>(wdata), 1);
                    }
                }
            }
        }
    }
    update_mip();
}

}  // namespace simrv::device
