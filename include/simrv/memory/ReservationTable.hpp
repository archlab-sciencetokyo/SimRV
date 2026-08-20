/**
 * @file ReservationTable.hpp
 * @brief Thread-safe LR/SC reservation tracking for multi-hart RISC-V systems.
 */
#pragma once

#include <mutex>
#include <optional>
#include <unordered_map>

#include "simrv/Define.hpp"
#include "simrv/xlen/Types.hpp"

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
    /// Default reservation granule: 64-byte aligned block (covers standard cache lines).
    static constexpr Address kReservationGranule = 64;
    static constexpr Address kReservationGranuleMask = ~(kReservationGranule - 1);

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
    void set_reservation(HartId hart, Address addr) {
        const std::scoped_lock lock(mutex_);
        reservations_[hart] = ReservationEntry{
            .address = addr & kReservationGranuleMask,
            .valid = true,
        };
    }

    /**
     * @brief Check if a Hart holds a valid reservation matching the target address.
     * @param hart Hart ID performing the SC check.
     * @param addr Physical address targeted by SC.
     * @return True if valid and matching, false otherwise.
     */
    [[nodiscard]] auto check_reservation(HartId hart, Address addr) const -> bool {
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
    auto check_and_clear_reservation(HartId hart, Address addr) -> bool {
        const std::scoped_lock lock(mutex_);
        auto it = reservations_.find(hart);
        if (it == reservations_.end() || !it->second.valid) {
            return false;
        }
        if (it->second.address == (addr & kReservationGranuleMask)) {
            it->second.valid = false;
            return true;
        }
        return false;
    }

    /**
     * @brief Invalidate all reservations that overlap the specified address granule.
     * @param addr Physical address written by a store, AMO, or DMA.
     * @param store_origin Optional Hart ID initiating the store. If provided,
     *                     reservations belonging to other Harts are invalidated.
     */
    void invalidate_matching(Address addr, std::optional<HartId> store_origin = std::nullopt) {
        const std::scoped_lock lock(mutex_);
        const Address granule_addr = addr & kReservationGranuleMask;
        for (auto& [hart, entry] : reservations_) {
            if (store_origin.has_value() && hart == *store_origin) {
                continue;  // Store originating from this Hart does not invalidate its own before SC
            }
            if (entry.valid && entry.address == granule_addr) {
                entry.valid = false;
            }
        }
    }

    /**
     * @brief Clear active reservation for a specific Hart.
     * @param hart Hart ID whose reservation to invalidate.
     */
    void clear_reservation(HartId hart) {
        const std::scoped_lock lock(mutex_);
        auto it = reservations_.find(hart);
        if (it != reservations_.end()) {
            it->second.valid = false;
        }
    }

    /**
     * @brief Invalidate all reservations across all Harts (e.g. on context switch/reset).
     */
    void clear_all() {
        const std::scoped_lock lock(mutex_);
        reservations_.clear();
    }

   private:
    struct ReservationEntry {
        Address address{};
        bool valid{false};
    };

    mutable std::mutex mutex_;
    std::unordered_map<HartId, ReservationEntry> reservations_;
};

}  // namespace simrv::memory
