/** @file TileLinkProtocolChecker.cpp */
#include "simrv/memory/TileLinkProtocolChecker.hpp"

#include <format>

namespace simrv::memory {

auto TileLinkProtocolChecker::accept_a(const TlChannelA& request)
    -> std::expected<void, std::string> {
    if (request.size > kTlBlockSize) {
        return std::unexpected(
            std::format("A source {} has illegal size {}", request.source, request.size));
    }
    const auto bytes = static_cast<Address>(Address{1} << request.size);
    if ((request.address & (bytes - 1u)) != 0) {
        return std::unexpected(
            std::format("A source {} address is not naturally aligned", request.source));
    }
    const bool coherent =
        request.opcode == TlOpcodeA::AcquireBlock || request.opcode == TlOpcodeA::AcquirePerm;
    if (coherent && request.size != kTlBlockSize) {
        return std::unexpected(
            std::format("A source {} acquire is not one cache block", request.source));
    }
    if (!coherent && request.size > kTlBeatSize) {
        return std::unexpected(
            std::format("A source {} uncached transfer exceeds one beat", request.source));
    }
    const TlMask natural_mask = TlChannelA::compute_mask(request.size, request.address);
    if (!coherent && request.mask != 0) {
        const bool partial = request.opcode == TlOpcodeA::PutPartialData;
        const bool valid_mask =
            partial ? ((request.mask & ~natural_mask) == 0) : request.mask == natural_mask;
        if (!valid_mask) {
            return std::unexpected(
                std::format("A source {} has an illegal byte mask", request.source));
        }
    }
    if (!sources_.emplace(request.source, request).second) {
        return std::unexpected(
            std::format("A source {} was reused before its response", request.source));
    }
    return {};
}

auto TileLinkProtocolChecker::accept_d(const TlChannelD& response)
    -> std::expected<void, std::string> {
    const auto source = sources_.find(response.source);
    if (source == sources_.end()) {
        return std::unexpected(
            std::format("D response references idle source {}", response.source));
    }
    const auto request_opcode = source->second.opcode;
    const bool response_matches = [&] {
        switch (request_opcode) {
            case TlOpcodeA::PutFullData:
            case TlOpcodeA::PutPartialData:
                return response.opcode == TlOpcodeD::AccessAck;
            case TlOpcodeA::Get:
            case TlOpcodeA::ArithmeticData:
            case TlOpcodeA::LogicalData:
                return response.opcode == TlOpcodeD::AccessAckData;
            case TlOpcodeA::Intent:
                return response.opcode == TlOpcodeD::HintAck;
            case TlOpcodeA::AcquireBlock:
                return response.opcode == TlOpcodeD::GrantData;
            case TlOpcodeA::AcquirePerm:
                return response.opcode == TlOpcodeD::Grant;
        }
        return false;
    }();
    if (!response_matches) {
        return std::unexpected(std::format("D opcode does not match A source {}", response.source));
    }
    if (response.size != source->second.size) {
        return std::unexpected(std::format("D size does not match A source {}", response.source));
    }
    const bool grant =
        response.opcode == TlOpcodeD::Grant || response.opcode == TlOpcodeD::GrantData;
    if (grant) {
        if (response.sink == 0) {
            return std::unexpected("D grant uses reserved sink 0");
        }
        if (!sinks_.emplace(response.sink, response.source).second) {
            return std::unexpected(std::format("D grant reused live sink {}", response.sink));
        }
    }
    sources_.erase(source);
    return {};
}

auto TileLinkProtocolChecker::accept_e(const TlChannelE& acknowledgement)
    -> std::expected<void, std::string> {
    if (acknowledgement.opcode != TlOpcodeE::GrantAck) {
        return std::unexpected("E channel contains an unsupported opcode");
    }
    if (sinks_.erase(acknowledgement.sink) != 1) {
        return std::unexpected(
            std::format("E acknowledgement references idle sink {}", acknowledgement.sink));
    }
    return {};
}

void TileLinkProtocolChecker::cancel(TlSourceId source) {
    sources_.erase(source);
    for (auto it = sinks_.begin(); it != sinks_.end();) {
        if (it->second == source) {
            it = sinks_.erase(it);
        } else {
            ++it;
        }
    }
}

void TileLinkProtocolChecker::reset() {
    sources_.clear();
    sinks_.clear();
}

}  // namespace simrv::memory
