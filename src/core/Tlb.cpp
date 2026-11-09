/**
 * @file Tlb.cpp
 * @brief Translation Lookaside Buffer (TLB) implementation.
 */
#include "simrv/core/Tlb.hpp"

namespace simrv::core {

void Tlb::flush() {
    if (simrv::compiler::unlikely(++inst_epoch_ == 0)) {
        inst_epoch_ = 1;
        for (auto& set : inst_r) set.fill(TLBEntry{});
        inst_r_lru.fill(0);
    }
    flush_data();
}

void Tlb::flush_data() {
    if (simrv::compiler::unlikely(++data_epoch_ == 0)) {
        data_epoch_ = 1;
        for (auto& set : data_r) set.fill(TLBEntry{});
        for (auto& set : data_w) set.fill(TLBEntry{});
        data_r_lru.fill(0);
        data_w_lru.fill(0);
    }
}

void Tlb::flush_selective(const TlbFlushFilter& filter) {
    if (!filter.vaddr && !filter.asid) {
        flush();
        return;
    }

    if (filter.vaddr) {
        const auto set = calc_set(filter.vaddr->raw());
        const simrv::xlen::Address vpage = calc_vpage(filter.vaddr->raw());
        auto clear_match = [&](TLBEntry& entry, uint32_t cur_epoch) {
            if (entry.valid && entry.epoch == cur_epoch && entry.v_addr == vpage) {
                if (filter.asid) {
                    if (!entry.global && entry.asid == *filter.asid) {
                        entry.valid = false;
                    }
                } else {
                    entry.valid = false;
                }
            }
        };
        for (int i = 0; i < 2; ++i) {
            clear_match(inst_r[set][i], inst_epoch_);
            clear_match(data_r[set][i], data_epoch_);
            clear_match(data_w[set][i], data_epoch_);
        }
        return;
    }

    if (filter.asid) {
        const Asid target_asid = *filter.asid;
        for (size_t s = 0; s < kNumSets; ++s) {
            for (int i = 0; i < 2; ++i) {
                if (inst_r[s][i].valid && inst_r[s][i].epoch == inst_epoch_ &&
                    !inst_r[s][i].global && inst_r[s][i].asid == target_asid) {
                    inst_r[s][i].valid = false;
                }
                if (data_r[s][i].valid && data_r[s][i].epoch == data_epoch_ &&
                    !data_r[s][i].global && data_r[s][i].asid == target_asid) {
                    data_r[s][i].valid = false;
                }
                if (data_w[s][i].valid && data_w[s][i].epoch == data_epoch_ &&
                    !data_w[s][i].global && data_w[s][i].asid == target_asid) {
                    data_w[s][i].valid = false;
                }
            }
        }
        return;
    }

    flush();
}

}  // namespace simrv::core
