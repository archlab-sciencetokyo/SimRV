/**
 * @file ReservationTable.hpp
 * @brief Thread-safe LR/SC reservation tracking for multi-hart RISC-V systems.
 */
#pragma once
#include <atomic>
#include <cstddef>
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "simrv/CompilerHints.hpp"
#include "simrv/xlen/Hash.hpp"

namespace simrv::memory {

/**
 * @class ReservationTable
 * @brief Manages Load-Reserved / Store-Conditional reservations across simulated Harts.
 *
 * Implements standard RISC-V LR/SC semantics where reservations are registered at a
 * cacheline / granule boundary and invalidated upon any store from another Hart.
 */
class ReservationTable {
   public:
    /// SimRV uses the minimum naturally aligned reservation set (eight bytes on RV64).
    static constexpr simrv::xlen::Address kReservationGranule = 8;
    static constexpr simrv::xlen::Address kReservationGranuleMask = ~(kReservationGranule - 1);

    ReservationTable() = default;
    ~ReservationTable() = default;

    ReservationTable(const ReservationTable&) = delete;
    auto operator=(const ReservationTable&) -> ReservationTable& = delete;
    ReservationTable(ReservationTable&&) = delete;
    auto operator=(ReservationTable&&) -> ReservationTable& = delete;

    /**
     * @brief Register a reservation for a given Hart.
     * @param hart Hart ID making the reservation.
     * @param addr Physical address loaded via LR.
     */
    void set_reservation(simrv::xlen::HartId hart, simrv::xlen::Address addr) {
        const std::scoped_lock lock(mutex_);
        reservations_[hart] = ReservationEntry{
            .address = addr & kReservationGranuleMask,
            .valid = true,
        };
        may_have_reservations_.store(true, std::memory_order_release);
    }

    /**
     * @brief Check if a Hart holds a valid reservation matching the target address.
     * @param hart Hart ID performing the SC check.
     * @param addr Physical address targeted by SC.
     * @return True if valid and matching, false otherwise.
     */
    [[nodiscard]] auto check_reservation(simrv::xlen::HartId hart, simrv::xlen::Address addr) const
        -> bool {
        const std::scoped_lock lock(mutex_);
        auto it = reservations_.find(hart);
        if (it == reservations_.end() || !it->second.valid) {
            return false;
        }
        return it->second.address == (addr & kReservationGranuleMask);
    }

    /**
     * @brief Atomically check and clear a reservation for SC execution.
     * @param hart Hart ID executing SC.
     * @param addr Target physical address of SC.
     * @return True if reservation was valid and cleared successfully, false otherwise.
     */
    auto check_and_clear_reservation(simrv::xlen::HartId hart, simrv::xlen::Address addr) -> bool {
        const std::scoped_lock lock(mutex_);
        auto it = reservations_.find(hart);
        if (it == reservations_.end() || !it->second.valid) {
            return false;
        }
        const bool success = it->second.address == (addr & kReservationGranuleMask);
        // Every SC attempt consumes the hart's reservation, including a failed
        // attempt whose address does not match the reserved set.
        it->second.valid = false;
        bool any_valid = false;
        for (const auto& [h, ent] : reservations_) {
            if (ent.valid) {
                any_valid = true;
                break;
            }
        }
        if (!any_valid) may_have_reservations_.store(false, std::memory_order_release);
        return success;
    }

    /**
     * @brief Invalidate all reservations that overlap the specified address granule.
     * @param addr Physical address written by a store, AMO, or DMA.
     * @param store_origin Optional Hart ID initiating the store. If provided,
     *                     reservations belonging to other Harts are invalidated.
     */
    void invalidate_matching(simrv::xlen::Address addr,
                             std::optional<simrv::xlen::HartId> store_origin = std::nullopt) {
        if (!may_have_reservations_.load(std::memory_order_acquire)) return;
        const std::scoped_lock lock(mutex_);
        const simrv::xlen::Address granule_addr = addr & kReservationGranuleMask;
        bool any_valid = false;
        for (auto& [hart, entry] : reservations_) {
            if (store_origin.has_value() && hart == *store_origin) {
                if (entry.valid) any_valid = true;
                continue;  // Store originating from this Hart does not invalidate its own before SC
            }
            if (entry.valid) {
                if (entry.address == granule_addr) {
                    entry.valid = false;
                } else {
                    any_valid = true;
                }
            }
        }
        if (!any_valid) {
            may_have_reservations_.store(false, std::memory_order_release);
        }
    }

    void invalidate_matching(simrv::xlen::PhysAddr addr,
                             std::optional<simrv::xlen::HartId> store_origin = std::nullopt) {
        invalidate_matching(addr.raw(), store_origin);
    }

    /**
     * @brief Invalidate reservations overlapping a physical byte range.
     * @param addr First physical byte written.
     * @param size Number of bytes written.
     * @param store_origin Optional Hart ID whose reservation remains valid.
     */
    void invalidate_range(simrv::xlen::Address addr, size_t size,
                          std::optional<simrv::xlen::HartId> store_origin = std::nullopt) {
        if (size == 0 || !may_have_reservations_.load(std::memory_order_acquire)) return;
        const simrv::xlen::Address first_granule = addr & kReservationGranuleMask;
        const auto extent = static_cast<simrv::xlen::Address>(size - 1);
        const simrv::xlen::Address last_byte =
            extent > std::numeric_limits<simrv::xlen::Address>::max() - addr
                ? std::numeric_limits<simrv::xlen::Address>::max()
                : addr + extent;
        const simrv::xlen::Address last_granule = last_byte & kReservationGranuleMask;
        const std::scoped_lock lock(mutex_);
        bool any_valid = false;
        for (auto& [hart, entry] : reservations_) {
            if (!entry.valid) continue;
            if (store_origin.has_value() && hart == *store_origin) {
                any_valid = true;
                continue;
            }
            if (entry.address >= first_granule && entry.address <= last_granule) {
                entry.valid = false;
            } else {
                any_valid = true;
            }
        }
        if (!any_valid) may_have_reservations_.store(false, std::memory_order_release);
    }

    void invalidate_range(simrv::xlen::PhysAddr addr, size_t size,
                          std::optional<simrv::xlen::HartId> store_origin = std::nullopt) {
        invalidate_range(addr.raw(), size, store_origin);
    }

    /**
     * @brief Clear active reservation for a specific Hart.
     * @param hart Hart ID whose reservation to invalidate.
     */
    void clear_reservation(simrv::xlen::HartId hart) {
        const std::scoped_lock lock(mutex_);
        auto it = reservations_.find(hart);
        if (it != reservations_.end()) {
            it->second.valid = false;
        }
        bool any_valid = false;
        for (const auto& [h, ent] : reservations_) {
            if (ent.valid) {
                any_valid = true;
                break;
            }
        }
        if (!any_valid) {
            may_have_reservations_.store(false, std::memory_order_release);
        }
    }

    /**
     * @brief Invalidate all reservations across all Harts (e.g. on context switch/reset).
     */
    void clear_all() {
        const std::scoped_lock lock(mutex_);
        reservations_.clear();
        may_have_reservations_.store(false, std::memory_order_release);
    }

    /// False guarantees that an ordinary store has no LR/SC reservation to invalidate. True is
    /// deliberately conservative until reset, keeping the common no-LR workload lock-free.
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto may_have_reservations() const noexcept -> bool {
        return may_have_reservations_.load(std::memory_order_acquire);
    }

   private:
    struct ReservationEntry {
        simrv::xlen::Address address{};
        bool valid{false};
    };

    mutable std::mutex mutex_;
    std::unordered_map<simrv::xlen::HartId, ReservationEntry> reservations_;
    std::atomic<bool> may_have_reservations_{false};
};

}  // namespace simrv::memory
