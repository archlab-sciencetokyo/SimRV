/**
 * @file DecodeCache.hpp
 * @brief Fast direct-mapped instruction decode cache for simulator acceleration.
 */
#pragma once

#include <array>

#include "simrv/Define.hpp"
#include "simrv/pipeline/DecodedInstruction.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

/**
 * @enum FastMemClass
 * @brief Precomputed memory access category for accelerated cache dispatch.
 */
enum class FastMemClass : uint8_t {
    None = 0,
    IntLoadSigned,
    IntLoadUnsigned,
    IntStore,
    FpLoad,
    FpStore,
};

/**
 * @struct CachedOp
 * @brief Compact decoded instruction payload used by the fast execution path.
 */
struct CachedOp {
    VirtAddr cpc{0};
    ImmValue imm = 0;
    Instruction ir = 0;
    uint16_t cinsn = 0;
    isa::OperationId op_id = isa::UNKNOWN;
    simrv::pipeline::DependencyTraits traits{};
    RegId rd = RegId::Zero;
    RegId rs1 = RegId::Zero;
    RegId rs2 = RegId::Zero;
    isa::Funct3 funct3 = static_cast<isa::Funct3>(0);
    FastMemClass mem_class : 4 = FastMemClass::None;
    uint8_t mem_size : 4 = 0;
    bool valid : 1 = false;
    uint8_t next_victim : 1 = 0;
    uint8_t reserved : 6 = 0;

    [[nodiscard]] constexpr auto len() const noexcept -> uint8_t { return cinsn ? 2u : 4u; }
    [[nodiscard]] constexpr auto align_mask() const noexcept -> uint8_t {
        return static_cast<uint8_t>(mem_size ? (mem_size - 1u) : 0u);
    }
    [[nodiscard]] constexpr auto opcode() const noexcept -> isa::Opcode {
        return static_cast<isa::Opcode>(ir & 0x7Fu);
    }
    [[nodiscard]] constexpr auto funct7() const noexcept -> Funct7 {
        return static_cast<Funct7>((ir >> 25u) & 0x7Fu);
    }
    [[nodiscard]] constexpr auto funct5() const noexcept -> isa::Funct5Amo {
        return static_cast<isa::Funct5Amo>((ir >> 27u) & 0x1Fu);
    }
    [[nodiscard]] constexpr auto funct12() const noexcept -> Funct12 {
        return static_cast<Funct12>(ir >> 20u);
    }
    [[nodiscard]] constexpr auto ir_org() const noexcept -> Instruction {
        return cinsn ? static_cast<Instruction>(cinsn) : ir;
    }

    constexpr void copy_from(const simrv::pipeline::DecodedInstruction& decoded) noexcept {
        cpc = decoded.cpc;
        imm = decoded.imm;
        ir = decoded.ir;
        cinsn = static_cast<uint16_t>(decoded.cinsn);
        op_id = decoded.op_id;
        rd = decoded.rd;
        rs1 = decoded.rs1;
        rs2 = decoded.rs2;
        funct3 = decoded.funct3;
        traits = decoded.traits;
        if (decoded.traits.is_mem_load) {
            mem_size = static_cast<uint8_t>(1u << (static_cast<unsigned>(decoded.funct3) & 0x3u));
            if (decoded.traits.writes_fp) {
                mem_class = FastMemClass::FpLoad;
            } else if ((static_cast<unsigned>(decoded.funct3) & 0x4u) != 0) {
                mem_class = FastMemClass::IntLoadUnsigned;
            } else {
                mem_class = FastMemClass::IntLoadSigned;
            }
        } else if (decoded.traits.is_mem_store) {
            mem_size = static_cast<uint8_t>(1u << (static_cast<unsigned>(decoded.funct3) & 0x3u));
            if (decoded.traits.reads_rs2_fp) {
                mem_class = FastMemClass::FpStore;
            } else {
                mem_class = FastMemClass::IntStore;
            }
        } else {
            mem_size = 0;
            mem_class = FastMemClass::None;
        }
    }

    constexpr void copy_to(simrv::pipeline::DecodedInstruction& decoded) const noexcept {
        decoded.cpc = cpc;
        decoded.imm = imm;
        decoded.pending_tval = 0;
        decoded.pending_exception = std::nullopt;
        decoded.ir = ir;
        decoded.ir_org = ir_org();
        decoded.cinsn = cinsn;
        decoded.op_id = op_id;
        decoded.funct7 = funct7();
        decoded.funct12 = funct12();
        decoded.opcode = opcode();
        decoded.rd = rd;
        decoded.rs1 = rs1;
        decoded.rs2 = rs2;
        decoded.funct3 = funct3;
        decoded.funct5 = funct5();
        decoded.traits = traits;
    }
};

/**
 * @class DecodeCache
 * @brief 2-way set-associative cache for pre-decoded instructions to bypass fetch/decode stages.
 *
 * Indexed by XOR-hash of PC (`((pc >> 1) ^ (pc >> 13)) & kSetMask`), caching 4096 decoded
 * instructions (2048 2-way sets) with 64-byte set alignment matching exactly one host L1 D-cache
 * line and 1-bit round-robin replacement. Lookup hits are read-only so the hottest path does not
 * dirty cache metadata.
 */
class DecodeCache {
   public:
    static constexpr size_t kNumSets = 2048;            ///< Number of 2-way associative sets
    static constexpr size_t kSetMask = kNumSets - 1;    ///< Bitmask for set index calculation
    static constexpr size_t kCacheSize = kNumSets * 2;  ///< Total cached instruction entries (4096)

    struct alignas(64) CacheSet {
        std::array<CachedOp, 2> ways{};

        [[nodiscard]] constexpr auto next_victim() const noexcept -> uint8_t {
            return ways[0].next_victim;
        }
        constexpr void set_next_victim(uint8_t v) noexcept {
            ways[0].next_victim = static_cast<uint8_t>(v & 0x1u);
        }
    };

    /**
     * @brief Invalidate all entries in the decode cache.
     */
    void flush() {
        for (auto& set : sets_) {
            set.ways[0].valid = false;
            set.ways[0].cpc = VirtAddr{~Register{0}};
            set.ways[1].valid = false;
            set.ways[1].cpc = VirtAddr{~Register{0}};
            set.set_next_victim(0);
        }
    }

    /**
     * @brief Invalidate all entries in the decode cache residing in a specific 4KB virtual page.
     * @param vpage 4KB-aligned virtual page address.
     */
    void flush_page(Address vpage) {
        const Address page_base = vpage & ~Address{0xFFF};
        for (auto& set : sets_) {
            if (set.ways[0].valid && ((set.ways[0].cpc.raw() & ~Address{0xFFF}) == page_base)) {
                set.ways[0].valid = false;
                set.ways[0].cpc = VirtAddr{~Register{0}};
            }
            if (set.ways[1].valid && ((set.ways[1].cpc.raw() & ~Address{0xFFF}) == page_base)) {
                set.ways[1].valid = false;
                set.ways[1].cpc = VirtAddr{~Register{0}};
            }
        }
    }

    /**
     * @brief Calculate the cache set index for a given program counter with bit-mixed XOR hashing.
     * @param pc Program counter address.
     * @return Cache set index in range [0, kNumSets - 1].
     */
    [[nodiscard]] static constexpr inline auto calc_set(Register pc) noexcept -> size_t {
        return ((pc >> 1) ^ (pc >> 13)) & kSetMask;
    }
    [[nodiscard]] static constexpr inline auto calc_set(VirtAddr pc) noexcept -> size_t {
        return ((pc.raw() >> 1) ^ (pc.raw() >> 13)) & kSetMask;
    }

    /**
     * @brief Lookup pre-decoded operation for given program counter.
     * @param pc Program counter address.
     * @return Pointer to CachedOp if hit, or nullptr on miss.
     */
    [[nodiscard]] inline auto lookup(Register pc) noexcept -> CachedOp* {
        const size_t set_idx = calc_set(pc);
        auto& set = sets_[set_idx];
        if (simrv::compiler::likely(set.ways[0].valid && set.ways[0].cpc == pc)) {
            return &set.ways[0];
        }
        if (set.ways[1].valid && set.ways[1].cpc == pc) {
            return &set.ways[1];
        }
        return nullptr;
    }
    [[nodiscard]] inline auto lookup(VirtAddr pc) noexcept -> CachedOp* { return lookup(pc.raw()); }

    /**
     * @brief Insert pre-decoded operation into the decode cache using LRU replacement.
     * @param pc Program counter address.
     * @param op Decoded instruction payload.
     */
    inline void insert(Register pc, const CachedOp& op) {
        const size_t set_idx = calc_set(pc);
        auto& set = sets_[set_idx];
        uint8_t way = 0;
        if (set.ways[0].valid && set.ways[0].cpc == pc) {
            way = 0;
        } else if (set.ways[1].valid && set.ways[1].cpc == pc) {
            way = 1;
        } else if (!set.ways[0].valid) {
            way = 0;
        } else if (!set.ways[1].valid) {
            way = 1;
        } else {
            way = set.next_victim();
        }
        auto& entry = set.ways[way];
        entry = op;
        entry.cpc = VirtAddr{pc};
        entry.valid = true;
        set.set_next_victim(static_cast<uint8_t>(1U - way));
    }
    inline void insert(VirtAddr pc, const CachedOp& op) { insert(pc.raw(), op); }

   private:
    std::array<CacheSet, kNumSets> sets_{};
};

static_assert(sizeof(CachedOp) == (sizeof(Address) == 8 ? 32 : 24));
static_assert(sizeof(DecodeCache::CacheSet) == 64);
static_assert(sizeof(DecodeCache) == 128 * 1024);

}  // namespace simrv::core
