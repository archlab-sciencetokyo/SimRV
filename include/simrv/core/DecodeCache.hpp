/**
 * @file DecodeCache.hpp
 * @brief Fast set-associative instruction decode cache for simulator acceleration.
 */
#pragma once

#include <array>

#include "simrv/CompilerHints.hpp"
#include "simrv/pipeline/DecodedInstruction.hpp"
#include "simrv/pipeline/OperationTraits.hpp"
#include "simrv/xlen/AddressTypes.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

namespace simrv::core {

/**
 * @enum FastDispatchClass
 * @brief Dense category tag for accelerated execution dispatch.
 */
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
 * @enum FastOp
 * @brief Unified dense dispatch tag combining high-frequency ALUs and operation categories.
 */
enum class FastOp : uint8_t {
    // Dominant Tier-1 ALUs (87.3% of ALL ALUs machine-wide): 0 .. 3
    Add = 0,   // Unifies ADD & ADDI (42% of ALUs)
    Addw = 1,  // Unifies ADDW & ADDIW (25% of ALUs)
    Slli = 2,  // SLLI (17% of ALUs)
    Srli = 3,  // SRLI (15% of ALUs)

    // Dominant Tier-2 ALUs (11% of ALL ALUs): 4 .. 7
    Andi = 4,   // ANDI (6.9%)
    Mulw = 5,   // MULW (5.6%)
    Or = 6,     // Unifies OR & ORI (4.5%)
    Slliw = 7,  // SLLIW (4.5%)

    // Dominant Tier-3 ALUs: 8 .. 11
    And = 8,    // AND (4.3%)
    Srliw = 9,  // SRLIW (3.6%)
    Sub = 10,   // SUB (0.3%)
    Xor = 11,   // Unifies XOR & XORI (0.2%)

    // Other scalar ALUs: 12 .. 43
    Addi = 12,
    Addiw = 13,
    Ori = 14,
    Xori = 15,
    Srai = 16,
    Sll = 17,
    Srl = 18,
    Sra = 19,
    Slt = 20,
    Sltu = 21,
    Slti = 22,
    Sltiu = 23,
    Mul = 24,
    Mulh = 25,
    Mulhsu = 26,
    Mulhu = 27,
    Div = 28,
    Divu = 29,
    Rem = 30,
    Remu = 31,
    Lui = 32,
    Auipc = 33,
    Subw = 34,
    Sllw = 35,
    Srlw = 36,
    Sraw = 37,
    Divw = 38,
    Divuw = 39,
    Remw = 40,
    Remuw = 41,
    Sraiw = 42,
    Bitmanip = 43,

    // Marker boundary: all values < kFirstNonAlu are ALU operations
    kFirstNonAlu = 48,

    // Specialized non-ALU execution operations
    Ld = 48,
    Lw = 49,
    Lwu = 50,
    Lh = 51,
    Lhu = 52,
    Lb = 53,
    Lbu = 54,

    Sd = 55,
    Sw = 56,
    Sh = 57,
    Sb = 58,

    Jal = 59,
    Jalr = 60,
    Bne = 61,
    Beq = 62,
    Bgeu = 63,
    Bge = 64,
    Bltu = 65,
    Blt = 66,

    // Legacy and complex fallback categories
    IntLoad = 67,
    Branch = 68,
    IntStore = 69,
    FpMem = 70,
    Other = 71,
};

[[nodiscard]] constexpr auto is_alu_fast_op(FastOp op) noexcept -> bool {
    return op < FastOp::kFirstNonAlu;
}

[[nodiscard]] constexpr auto is_int_load_fast_op(FastOp op) noexcept -> bool {
    return (op >= FastOp::Ld && op <= FastOp::Lbu) || op == FastOp::IntLoad;
}

[[nodiscard]] constexpr auto is_int_store_fast_op(FastOp op) noexcept -> bool {
    return (op >= FastOp::Sd && op <= FastOp::Sb) || op == FastOp::IntStore;
}

[[nodiscard]] constexpr auto is_branch_fast_op(FastOp op) noexcept -> bool {
    return (op >= FastOp::Jal && op <= FastOp::Blt) || op == FastOp::Branch;
}

using FastAluKind = FastOp;
using FastDispatchClass = FastOp;

enum class BranchKind : uint8_t {
    Jal = 0,
    Jalr = 1,
    Bne = 2,
    Beq = 3,
    Bgeu = 4,
    Bge = 5,
    Bltu = 6,
    Blt = 7,
    None = 8
};

static constexpr uint16_t kSentinelSeqIndex = 4096u;
static constexpr uint16_t kInvalidSeqIndex = kSentinelSeqIndex;

/**
 * @struct CachedOp
 * @brief Compact decoded instruction payload used by the fast execution path.
 */
struct CachedOp {
    // The all-ones PC is odd and cannot identify a RISC-V instruction. It doubles as
    // the invalid-entry tag so hot lookups need only compare the cached PC.
    simrv::xlen::VirtAddr cpc{~simrv::xlen::Register{0}};
    // All encoded RISC-V immediates, including U-type values, fit in a signed 32-bit value.
    // Keeping this narrow avoids growing the decode-cache entry or disturbing its
    // two-entries-per-cache-line layout.
    int32_t imm = 0;
    simrv::xlen::Instruction ir = 0;
    uint16_t cinsn = 0;
    isa::OperationId op_id = isa::UNKNOWN;
    uint16_t next_seq_idx = kInvalidSeqIndex;
    uint16_t target_idx = kInvalidSeqIndex;
    simrv::xlen::RegId rd = simrv::xlen::RegId::Zero;
    simrv::xlen::RegId rs1 = simrv::xlen::RegId::Zero;
    simrv::xlen::RegId rs2 = simrv::xlen::RegId::Zero;
    FastOp fast_op = FastOp::Other;
    BranchKind branch_kind : 4 = BranchKind::None;
    isa::Funct3 funct3 : 3 = isa::Funct3::Add;
    uint8_t valid : 1 = 0;
    FastMemClass mem_class : 3 = FastMemClass::None;
    uint8_t is_straight_alu : 1 = 0;
    uint8_t mem_size : 4 = 0;
    uint8_t op_len = 4;
    uint8_t next_victim : 1 = 0;
    uint8_t reserved : 7 = 0;
    [[nodiscard]] constexpr auto len() const noexcept -> uint8_t { return op_len; }
    [[nodiscard]] constexpr auto is_compressed() const noexcept -> bool { return op_len == 2u; }
    [[nodiscard]] constexpr auto align_mask() const noexcept -> uint8_t {
        return static_cast<uint8_t>(mem_size ? (mem_size - 1u) : 0u);
    }
    [[nodiscard]] constexpr auto opcode() const noexcept -> isa::Opcode {
        return static_cast<isa::Opcode>(ir & 0x7Fu);
    }
    [[nodiscard]] constexpr auto funct7() const noexcept -> simrv::xlen::Funct7 {
        return static_cast<simrv::xlen::Funct7>((ir >> 25u) & 0x7Fu);
    }
    [[nodiscard]] constexpr auto funct5() const noexcept -> isa::Funct5Amo {
        return static_cast<isa::Funct5Amo>((ir >> 27u) & 0x1Fu);
    }
    [[nodiscard]] constexpr auto funct12() const noexcept -> simrv::xlen::Funct12 {
        return static_cast<simrv::xlen::Funct12>(ir >> 20u);
    }
    [[nodiscard]] constexpr auto ir_org() const noexcept -> simrv::xlen::Instruction {
        return cinsn ? static_cast<simrv::xlen::Instruction>(cinsn) : ir;
    }

    [[nodiscard]] static constexpr auto classify_fast_alu(isa::OperationId op) noexcept -> FastOp {
        if (isa::is_zb_op(op)) {
            return FastOp::Bitmanip;
        }
        using enum FastOp;
        switch (op) {
            case isa::ADD:
            case isa::ADDI:
                return Add;
            case isa::SUB:
                return Sub;
            case isa::AND:
                return And;
            case isa::OR:
            case isa::ORI:
                return Or;
            case isa::XOR:
            case isa::XORI:
                return Xor;
            case isa::SLL:
                return Sll;
            case isa::SRL:
                return Srl;
            case isa::SRA:
                return Sra;
            case isa::SLT:
                return Slt;
            case isa::SLTU:
                return Sltu;
            case isa::MUL:
                return Mul;
            case isa::MULH:
                return Mulh;
            case isa::MULHSU:
                return Mulhsu;
            case isa::MULHU:
                return Mulhu;
            case isa::DIV:
                return Div;
            case isa::DIVU:
                return Divu;
            case isa::REM:
                return Rem;
            case isa::REMU:
                return Remu;
            case isa::ANDI:
                return Andi;
            case isa::SLLI:
                return Slli;
            case isa::SRLI:
                return Srli;
            case isa::SRAI:
                return Srai;
            case isa::SLTI:
                return Slti;
            case isa::SLTIU:
                return Sltiu;
            case isa::LUI:
                return Lui;
            case isa::AUIPC:
                return Auipc;
#if SIMRV_XLEN == 64
            case isa::ADDW:
            case isa::ADDIW:
                return Addw;
            case isa::SUBW:
                return Subw;
            case isa::SLLW:
                return Sllw;
            case isa::SRLW:
                return Srlw;
            case isa::SRAW:
                return Sraw;
            case isa::MULW:
                return Mulw;
            case isa::DIVW:
                return Divw;
            case isa::DIVUW:
                return Divuw;
            case isa::REMW:
                return Remw;
            case isa::REMUW:
                return Remuw;
            case isa::SLLIW:
                return Slliw;
            case isa::SRLIW:
                return Srliw;
            case isa::SRAIW:
                return Sraiw;
#endif
            default:
                return Other;
        }
    }

    [[nodiscard]] static constexpr auto is_fast_alu_op(isa::OperationId op) noexcept -> bool {
        return classify_fast_alu(op) < FastOp::kFirstNonAlu;
    }

    [[nodiscard]] constexpr auto is_alu() const noexcept -> bool {
        return fast_op < FastOp::kFirstNonAlu;
    }

    [[nodiscard]] constexpr auto has_fast_alu() const noexcept -> bool { return is_alu(); }

    [[nodiscard]] constexpr auto fast_alu_kind() const noexcept -> FastOp { return fast_op; }

    constexpr void set_fast_alu_kind(FastOp kind) noexcept { fast_op = kind; }

    [[nodiscard]] static constexpr auto classify_branch(isa::OperationId op) noexcept
        -> BranchKind {
        using enum isa::OperationId;
        switch (op) {
            case BEQ:
                return BranchKind::Beq;
            case BNE:
                return BranchKind::Bne;
            case BLT:
                return BranchKind::Blt;
            case BGE:
                return BranchKind::Bge;
            case BLTU:
                return BranchKind::Bltu;
            case BGEU:
                return BranchKind::Bgeu;
            case JAL:
                return BranchKind::Jal;
            case JALR:
                return BranchKind::Jalr;
            default:
                return BranchKind::None;
        }
    }

    [[nodiscard]] static constexpr auto is_fast_branch_op(isa::OperationId op) noexcept -> bool {
        return classify_branch(op) != BranchKind::None;
    }

    constexpr void copy_from(const simrv::pipeline::DecodedInstruction& decoded) noexcept {
        cpc = decoded.cpc;
        imm = static_cast<int32_t>(decoded.imm);
        ir = decoded.ir;
        cinsn = static_cast<uint16_t>(decoded.cinsn);
        op_id = decoded.op_id;
        rd = (decoded.rd == simrv::xlen::RegId::Zero && !decoded.traits.writes_fp)
                 ? simrv::xlen::RegId::Discard
                 : decoded.rd;
        rs1 = decoded.rs1;
        rs2 = decoded.rs2;
        if (decoded.op_id == isa::ADD || decoded.op_id == isa::ADDW || decoded.op_id == isa::OR ||
            decoded.op_id == isa::XOR) {
            imm = 0;
        } else if (decoded.op_id == isa::ADDI || decoded.op_id == isa::ADDIW ||
                   decoded.op_id == isa::ORI || decoded.op_id == isa::XORI) {
            rs2 = simrv::xlen::RegId::Zero;
        }
        funct3 = decoded.funct3;
        next_seq_idx = kInvalidSeqIndex;
        target_idx = kInvalidSeqIndex;
        op_len = static_cast<uint8_t>(decoded.cinsn != 0 ? 2u : 4u);
        is_straight_alu = !decoded.traits.is_control && !decoded.traits.is_mem_load &&
                          !decoded.traits.is_mem_store && !decoded.traits.is_serializing &&
                          !decoded.traits.is_cfu && !decoded.traits.writes_vec &&
                          !decoded.traits.writes_fp && !decoded.traits.reads_rs1_fp &&
                          !decoded.traits.reads_rs2_fp && !decoded.traits.reads_rs3_fp &&
                          !decoded.pending_exception.has_value() && decoded.op_id != isa::UNKNOWN;
        const auto alu_op = is_straight_alu ? classify_fast_alu(decoded.op_id) : FastOp::Other;
        const auto br_kind = !decoded.pending_exception.has_value() ? classify_branch(decoded.op_id)
                                                                    : BranchKind::None;
        branch_kind = br_kind;
        if (alu_op != FastOp::Other) {
            fast_op = alu_op;
            mem_size = 0;
            mem_class = FastMemClass::None;
            return;
        }
        if (br_kind != BranchKind::None) {
            switch (br_kind) {
                case BranchKind::Jal:
                    fast_op = FastOp::Jal;
                    break;
                case BranchKind::Jalr:
                    fast_op = FastOp::Jalr;
                    break;
                case BranchKind::Bne:
                    fast_op = FastOp::Bne;
                    break;
                case BranchKind::Beq:
                    fast_op = FastOp::Beq;
                    break;
                case BranchKind::Bgeu:
                    fast_op = FastOp::Bgeu;
                    break;
                case BranchKind::Bge:
                    fast_op = FastOp::Bge;
                    break;
                case BranchKind::Bltu:
                    fast_op = FastOp::Bltu;
                    break;
                case BranchKind::Blt:
                    fast_op = FastOp::Blt;
                    break;
                default:
                    fast_op = FastOp::Branch;
                    break;
            }
            mem_size = 0;
            mem_class = FastMemClass::None;
            return;
        }
        using enum isa::OperationId;
        switch (decoded.op_id) {
            case LB:
                mem_size = 1;
                mem_class = FastMemClass::IntLoadSigned;
                fast_op = FastOp::Lb;
                break;
            case LH:
                mem_size = 2;
                mem_class = FastMemClass::IntLoadSigned;
                fast_op = FastOp::Lh;
                break;
            case LW:
                mem_size = 4;
                mem_class = FastMemClass::IntLoadSigned;
                fast_op = FastOp::Lw;
                break;
            case LD:
                mem_size = 8;
                mem_class = FastMemClass::IntLoadSigned;
                fast_op = FastOp::Ld;
                break;
            case LBU:
                mem_size = 1;
                mem_class = FastMemClass::IntLoadUnsigned;
                fast_op = FastOp::Lbu;
                break;
            case LHU:
                mem_size = 2;
                mem_class = FastMemClass::IntLoadUnsigned;
                fast_op = FastOp::Lhu;
                break;
            case LWU:
                mem_size = 4;
                mem_class = FastMemClass::IntLoadUnsigned;
                fast_op = FastOp::Lwu;
                break;
            case SB:
                mem_size = 1;
                mem_class = FastMemClass::IntStore;
                fast_op = FastOp::Sb;
                break;
            case SH:
                mem_size = 2;
                mem_class = FastMemClass::IntStore;
                fast_op = FastOp::Sh;
                break;
            case SW:
                mem_size = 4;
                mem_class = FastMemClass::IntStore;
                fast_op = FastOp::Sw;
                break;
            case SD:
                mem_size = 8;
                mem_class = FastMemClass::IntStore;
                fast_op = FastOp::Sd;
                break;
            case FLW:
            case FLD:
                mem_size =
                    static_cast<uint8_t>(1u << (static_cast<unsigned>(decoded.funct3) & 0x3u));
                mem_class = FastMemClass::FpLoad;
                fast_op = FastOp::FpMem;
                break;
            case FSW:
            case FSD:
                mem_size =
                    static_cast<uint8_t>(1u << (static_cast<unsigned>(decoded.funct3) & 0x3u));
                mem_class = FastMemClass::FpStore;
                fast_op = FastOp::FpMem;
                break;
            default:
                mem_size = 0;
                mem_class = FastMemClass::None;
                fast_op = FastOp::Other;
                break;
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
        decoded.rd = (rd == simrv::xlen::RegId::Discard) ? simrv::xlen::RegId::Zero : rd;
        decoded.rs1 = rs1;
        decoded.rs2 = rs2;
        decoded.funct3 = funct3;
        decoded.funct5 = funct5();
        decoded.traits = simrv::pipeline::operation::make_dependency_traits(op_id, opcode(),
                                                                            decoded.rd, funct5());
    }
};

/**
 * @class DecodeCache
 * @brief 2-way set-associative cache for pre-decoded instructions to bypass fetch/decode stages.
 *
 * Indexed by the low instruction-address bits, caching 4096 decoded instructions (2048 2-way
 * sets) with 64-byte set alignment matching exactly one host L1 D-cache line and 1-bit
 * round-robin replacement. Lookup hits are read-only so the hottest path does not dirty cache
 * metadata.
 */
class DecodeCache {
   public:
    static constexpr size_t kNumSets = 2048;            ///< Number of 2-way associative sets
    static constexpr size_t kSetMask = kNumSets - 1;    ///< Bitmask for set index calculation
    static constexpr size_t kCacheSize = kNumSets * 2;  ///< Total cached instruction entries (4096)
    static_assert(kSentinelSeqIndex == kCacheSize);

    struct alignas(64) CacheSet {
        std::array<CachedOp, 2> ways{};

        [[nodiscard]] constexpr auto next_victim() const noexcept -> uint8_t {
            return ways[0].next_victim;
        }
        constexpr void set_next_victim(uint8_t v) noexcept {
            ways[0].next_victim = static_cast<uint8_t>(v & 0x1u);
        }
    };

    DecodeCache() noexcept { flush(); }

    constexpr void set_active_context([[maybe_unused]] uint8_t ctx) noexcept {}
    [[nodiscard]] constexpr auto active_context() const noexcept -> uint8_t { return 0; }

    [[nodiscard]] SIMRV_ALWAYS_INLINE auto ops_data() noexcept -> CachedOp* {
        return &sets_[0].ways[0];
    }
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto ops_data() const noexcept -> const CachedOp* {
        return &sets_[0].ways[0];
    }

    [[nodiscard]] SIMRV_ALWAYS_INLINE auto op_by_index(size_t idx) noexcept -> CachedOp* {
        if constexpr (sizeof(CachedOp) == 32) {
            return ops_data() + idx;
        } else {
            return &sets_[idx >> 1].ways[idx & 1];
        }
    }
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto op_by_index(size_t idx) const noexcept
        -> const CachedOp* {
        if constexpr (sizeof(CachedOp) == 32) {
            return ops_data() + idx;
        } else {
            return &sets_[idx >> 1].ways[idx & 1];
        }
    }
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto index_of(const CachedOp* op) const noexcept -> uint16_t {
        if constexpr (sizeof(CachedOp) == 32) {
            return static_cast<uint16_t>(op - ops_data());
        } else {
            const auto* set = set_of(op);
            const auto set_idx = static_cast<uint16_t>(set - sets_.data());
            const auto way = static_cast<uint16_t>(op == &set->ways[1] ? 1 : 0);
            return static_cast<uint16_t>((set_idx << 1) | way);
        }
    }

    /**
     * @brief Invalidate all entries in the decode cache.
     */
    void flush() {
        for (auto& set : sets_) {
            set.ways[0].valid = 0;
            set.ways[0].next_seq_idx = kInvalidSeqIndex;
            set.ways[0].target_idx = kInvalidSeqIndex;
            set.ways[0].cpc = simrv::xlen::VirtAddr{~simrv::xlen::Register{0}};
            set.ways[1].valid = 0;
            set.ways[1].next_seq_idx = kInvalidSeqIndex;
            set.ways[1].target_idx = kInvalidSeqIndex;
            set.ways[1].cpc = simrv::xlen::VirtAddr{~simrv::xlen::Register{0}};
            set.set_next_victim(0);
        }
    }

    /**
     * @brief Invalidate all entries in the decode cache residing in a specific 4KB virtual page.
     * @param vpage 4KB-aligned virtual page address.
     */
    void flush_page(simrv::xlen::Address vpage) {
        const simrv::xlen::Address page_base = vpage & ~simrv::xlen::Address{0xFFF};
        // With word-address indexing, a 4KB page spans 1024 cache sets (plus the preceding
        // halfword's set for a 32-bit instruction that straddles the page boundary).
        const size_t start_set = calc_set(page_base - 2);
        for (size_t i = 0; i <= 1024; ++i) {
            auto& set = sets_[(start_set + i) & kSetMask];
            if (set.ways[0].valid) {
                const auto cpc0 = set.ways[0].cpc.raw();
                if (((cpc0 & ~simrv::xlen::Address{0xFFF}) == page_base) ||
                    (((cpc0 + set.ways[0].len() - 1) & ~simrv::xlen::Address{0xFFF}) ==
                     page_base)) {
                    set.ways[0].valid = 0;
                    set.ways[0].next_seq_idx = kInvalidSeqIndex;
                    set.ways[0].target_idx = kInvalidSeqIndex;
                    set.ways[0].cpc = simrv::xlen::VirtAddr{~simrv::xlen::Register{0}};
                }
            }
            if (set.ways[1].valid) {
                const auto cpc1 = set.ways[1].cpc.raw();
                if (((cpc1 & ~simrv::xlen::Address{0xFFF}) == page_base) ||
                    (((cpc1 + set.ways[1].len() - 1) & ~simrv::xlen::Address{0xFFF}) ==
                     page_base)) {
                    set.ways[1].valid = 0;
                    set.ways[1].next_seq_idx = kInvalidSeqIndex;
                    set.ways[1].target_idx = kInvalidSeqIndex;
                    set.ways[1].cpc = simrv::xlen::VirtAddr{~simrv::xlen::Register{0}};
                }
            }
        }
    }

    /**
     * @brief Calculate the cache set index from the low instruction-word address bits.
     * @param pc Program counter address.
     * @return Cache set index in range [0, kNumSets - 1].
     */
    [[nodiscard]] static constexpr inline auto calc_set(simrv::xlen::Register pc) noexcept
        -> size_t {
        return (pc >> 2) & kSetMask;
    }
    [[nodiscard]] static constexpr inline auto calc_set(simrv::xlen::VirtAddr pc) noexcept
        -> size_t {
        return (pc.raw() >> 2) & kSetMask;
    }

    /**
     * @brief Lookup pre-decoded operation for given program counter.
     * @param pc Program counter address.
     * @return Pointer to CachedOp if hit, or nullptr on miss.
     */
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto lookup(simrv::xlen::Register pc) noexcept -> CachedOp* {
        const size_t set_idx = calc_set(pc);
        auto& set = sets_[set_idx];
        if (simrv::compiler::likely(set.ways[0].cpc == pc && set.ways[0].valid)) {
            return &set.ways[0];
        }
        if (set.ways[1].cpc == pc && set.ways[1].valid) {
            return &set.ways[1];
        }
        return nullptr;
    }
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto lookup(simrv::xlen::VirtAddr pc) noexcept -> CachedOp* {
        return lookup(pc.raw());
    }

    /**
     * @brief Look up a decoded operation for a known-aligned architectural PC.
     * @param pc Instruction address, which must be 2-byte aligned.
     * @return Pointer to CachedOp if hit, or nullptr on miss.
     *
     * The all-ones invalid-entry tag is odd, so aligned PCs cannot match invalid entries. This
     * variant avoids reading the separate valid bit on the fast execution path.
     */
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto lookup_aligned(simrv::xlen::Register pc) noexcept
        -> CachedOp* {
        // The invalid tag is odd, but a debugger can resume at any value. Let fetch handle the
        // all-ones address as a miss so its architecturally precise alignment trap is preserved.
        if (simrv::compiler::unlikely(pc == ~simrv::xlen::Register{0})) {
            return nullptr;
        }
        const size_t set_idx = calc_set(pc);
        auto& set = sets_[set_idx];
        if (simrv::compiler::likely(set.ways[0].cpc == pc)) {
            return &set.ways[0];
        }
        if (set.ways[1].cpc == pc) {
            return &set.ways[1];
        }
        return nullptr;
    }

    /**
     * @brief Look up the sequential successor using the predecessor's precomputed set index.
     * @param predecessor Cached operation whose successor index was recorded on insertion.
     * @param next_pc Sequential successor address.
     * @return Pointer to CachedOp if the expected successor is cached, or nullptr on a miss.
     */
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto lookup_next(const CachedOp&,
                                                       simrv::xlen::Register next_pc) noexcept
        -> CachedOp* {
        return lookup(next_pc);
    }

    /**
     * @brief Look up a sequential successor for a known-aligned architectural PC.
     * @param predecessor Cached operation whose successor set was recorded on insertion.
     * @param next_pc 2-byte-aligned sequential successor address.
     * @return Pointer to CachedOp if hit, or nullptr on miss.
     */
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto lookup_next_aligned(
        const CachedOp&, simrv::xlen::Register next_pc) noexcept -> CachedOp* {
        auto& set = sets_[calc_set(next_pc)];
        if (simrv::compiler::likely(set.ways[0].cpc == next_pc)) {
            return &set.ways[0];
        }
        if (set.ways[1].cpc == next_pc) {
            return &set.ways[1];
        }
        return nullptr;
    }

    /**
     * @brief Look up a taken branch or JAL target for a known-aligned architectural PC.
     * @param predecessor Cached branch/jump whose target set was recorded on insertion.
     * @param target_pc 2-byte-aligned branch target address.
     * @return Pointer to CachedOp if hit, or nullptr on miss.
     */
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto lookup_target_aligned(
        const CachedOp&, simrv::xlen::Register target_pc) noexcept -> CachedOp* {
        auto& set = sets_[calc_set(target_pc)];
        if (simrv::compiler::likely(set.ways[0].cpc == target_pc)) {
            return &set.ways[0];
        }
        if (set.ways[1].cpc == target_pc) {
            return &set.ways[1];
        }
        return nullptr;
    }

    /**
     * @brief Determine the enclosing CacheSet pointer from a CachedOp pointer.
     */
    [[nodiscard]] SIMRV_ALWAYS_INLINE static auto set_of(const CachedOp* op) noexcept
        -> const CacheSet* {
        return reinterpret_cast<const CacheSet*>(reinterpret_cast<uintptr_t>(op) & ~uintptr_t{63});
    }

    /**
     * @brief Advance to the next sequential CacheSet with single pointer addition and line-wrap.
     */
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto next_set(const CacheSet* current) const noexcept
        -> const CacheSet* {
        const CacheSet* next = current + 1;
        return (next != sets_.data() + kNumSets) ? next : sets_.data();
    }

    /**
     * @brief Look up the sequential successor in baremetal mode without hash arithmetic or context
     * check.
     */
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto lookup_sequential_baremetal(
        const CacheSet* candidate_set, simrv::xlen::Register next_pc) noexcept -> CachedOp* {
        if (simrv::compiler::likely(candidate_set->ways[0].cpc == next_pc)) {
            return const_cast<CachedOp*>(&candidate_set->ways[0]);
        }
        if (candidate_set->ways[1].cpc == next_pc) {
            return const_cast<CachedOp*>(&candidate_set->ways[1]);
        }
        return nullptr;
    }

    /**
     * @brief Look up an aligned instruction in baremetal mode without context comparison.
     */
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto lookup_baremetal_aligned(
        simrv::xlen::Register pc) noexcept -> CachedOp* {
        if (simrv::compiler::unlikely(pc == ~simrv::xlen::Register{0})) {
            return nullptr;
        }
        auto& set = sets_[calc_set(pc)];
        if (simrv::compiler::likely(set.ways[0].cpc == pc)) {
            return &set.ways[0];
        }
        if (set.ways[1].cpc == pc) {
            return &set.ways[1];
        }
        return nullptr;
    }

    /**
     * @brief Look up sequential successor in OS mode using candidate set pointer and active
     * context.
     */
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto lookup_sequential_aligned(
        const CacheSet* candidate_set, simrv::xlen::Register next_pc) noexcept -> CachedOp* {
        if (simrv::compiler::likely(candidate_set->ways[0].cpc == next_pc)) {
            return const_cast<CachedOp*>(&candidate_set->ways[0]);
        }
        if (candidate_set->ways[1].cpc == next_pc) {
            return const_cast<CachedOp*>(&candidate_set->ways[1]);
        }
        return nullptr;
    }

    /**
     * @brief Insert pre-decoded operation into the decode cache using LRU replacement.
     * @param pc Program counter address.
     * @param op Decoded instruction payload.
     */
    inline void insert(simrv::xlen::Register pc, const CachedOp& op) {
        const size_t set_idx = calc_set(pc);
        auto& set = sets_[set_idx];
        uint8_t way = 0;
        if (set.ways[0].cpc == pc && set.ways[0].valid) {
            way = 0;
        } else if (set.ways[1].cpc == pc && set.ways[1].valid) {
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
        entry.cpc = simrv::xlen::VirtAddr{pc};
        entry.valid = 1;
        entry.next_seq_idx = kInvalidSeqIndex;
        entry.target_idx = kInvalidSeqIndex;
        entry.op_len = static_cast<uint8_t>(op.cinsn != 0 ? 2u : (op.op_len ? op.op_len : 4u));
        set.set_next_victim(static_cast<uint8_t>(1U - way));
    }
    inline void insert(simrv::xlen::VirtAddr pc, const CachedOp& op) { insert(pc.raw(), op); }

   private:
    std::array<CacheSet, kNumSets + 1> sets_{};
};

static_assert(sizeof(CachedOp) == (sizeof(simrv::xlen::Address) == 8 ? 32 : 28));
static_assert(sizeof(DecodeCache::CacheSet) == 64);

}  // namespace simrv::core
