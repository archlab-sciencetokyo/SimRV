/**
 * @file TileLinkBus.cpp
 * @brief TileLink-style simple sequential bus implementation.
 */
#include "simrv/memory/TileLinkBus.hpp"

#include <cstdint>
#include <format>
#include <print>

#include "simrv/Define.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::memory {

using simrv::isa::Funct3;

TileLinkBus::TileLinkBus(simrv::core::Machine& machine) : machine_(machine) {}

void TileLinkBus::add_node(TileLinkNode* node) {
    if (node != nullptr) {
        nodes_.push_back(node);
    }
}

auto TileLinkBus::send_request(const TlChannelA& req) -> bool {
    req_queue_.push(req);
    return true;
}

void TileLinkBus::tick() {
    if (!req_queue_.empty()) {
        auto req = req_queue_.front();
        req_queue_.pop();

        TlChannelD resp{};
        resp.source = req.source;
        resp.size = req.size;
        resp.error = false;
        resp.data = 0;

        const Instruction funct3 = req.size;
        bool handled = false;

        if (req.opcode == TlOpcodeA::Get) {
            resp.opcode = TlOpcodeD::AccessAckData;
            if (simrv::memory::is_dram_addr(req.address) && machine_.mmem != nullptr) {
                resp.data = simrv::memory::ram_read_fast(req.address, funct3, machine_.mmem);
                ++read_count_;
                handled = true;
            }
        } else {
            resp.opcode = TlOpcodeD::AccessAck;
            const bool is_tohost_write =
                simrv::xlen::kIsXLen64 ? (funct3 == static_cast<Instruction>(Funct3::Sw) ||
                                           funct3 == static_cast<Instruction>(Funct3::Sd))
                                      : (funct3 == static_cast<Instruction>(Funct3::Sw));
            if (is_tohost_write) {
                if (req.address == machine_.s_isatest_tohost || req.address == 0x80001000 || req.address == 0x40008000) {
                    machine_.tohost = simrv::xlen::kIsXLen64
                                          ? req.data
                                          : ((machine_.tohost & 0xFFFFFFFF00000000ULL) | req.data);
                } else if (!simrv::xlen::kIsXLen64 &&
                           (req.address == machine_.s_isatest_tohost + 4 || req.address == 0x80001004 || req.address == 0x40008004)) {
                    machine_.tohost = (machine_.tohost & 0x00000000FFFFFFFFULL) |
                                      (static_cast<uint64_t>(req.data) << 32);
                }
            }

            if (simrv::memory::is_dram_addr(req.address) && machine_.mmem != nullptr) {
                simrv::memory::ram_write_fast(req.address, req.data, funct3, machine_.mmem);
                ++write_count_;
                handled = true;
            }
        }

        if (!handled) {
            for (auto* node : nodes_) {
                if (node->contains(req.address)) {
                    if (node->handle_request(req, resp)) {
                        if (machine_.s_debugmode) {
                            static int mmio_log_count = 0;
                            if (mmio_log_count < 64) {
                                simrv::log::info(
                                    "__ {:10} MMIO {:5} {:7} addr={:08x} data={:08x} f3={}",
                                    machine_.cpu.clint_mmio.mtime,
                                    req.opcode == TlOpcodeA::Get ? "read" : "write", node->name(),
                                    static_cast<unsigned>(req.address),
                                    static_cast<unsigned>(req.opcode == TlOpcodeA::Get ? resp.data
                                                                                       : req.data),
                                    static_cast<unsigned>(req.size));
                                ++mmio_log_count;
                            }
                        }
                        if (req.opcode == TlOpcodeA::Get)
                            ++read_count_;
                        else
                            ++write_count_;
                        handled = true;
                        break;
                    }
                }
            }
        }

        if (!handled) {
            resp.error = true;
        }
        resp_queue_.push_back(resp);
    }
}

auto TileLinkBus::get_response(uint8_t source_id, TlChannelD& resp) -> bool {
    while (true) {
        auto it = std::ranges::find_if(
            resp_queue_, [source_id](const auto& r) -> bool { return r.source == source_id; });
        if (it != resp_queue_.end()) {
            resp = *it;
            resp_queue_.erase(it);
            return true;
        }
        if (req_queue_.empty()) {
            break;
        }
        tick();
    }
    return false;
}

}  // namespace simrv::memory