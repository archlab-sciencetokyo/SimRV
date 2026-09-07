/** @file TileLinkProtocolChecker.hpp */
#pragma once

#include <expected>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "simrv/memory/TileLinkProtocol.hpp"

namespace simrv::memory {

class TileLinkProtocolChecker {
   public:
    [[nodiscard]] auto accept_a(const TlChannelA& request) -> std::expected<void, std::string>;
    [[nodiscard]] auto accept_b(const TlChannelB& probe) -> std::expected<void, std::string>;
    [[nodiscard]] auto accept_c(const TlChannelC& response) -> std::expected<void, std::string>;
    [[nodiscard]] auto accept_d(const TlChannelD& response) -> std::expected<void, std::string>;
    [[nodiscard]] auto accept_e(const TlChannelE& acknowledgement)
        -> std::expected<void, std::string>;
    void cancel(TlSourceId source);
    void reset();

    [[nodiscard]] auto outstanding_sources() const noexcept -> size_t { return sources_.size(); }
    [[nodiscard]] auto outstanding_probes() const noexcept -> size_t {
        return active_probes_.size();
    }
    [[nodiscard]] auto outstanding_releases() const noexcept -> size_t { return releases_.size(); }
    [[nodiscard]] auto outstanding_sinks() const noexcept -> size_t { return sinks_.size(); }

   private:
    std::unordered_map<TlSourceId, TlChannelA> sources_;
    std::unordered_map<TlSourceId, TlChannelC> releases_;
    std::unordered_set<Address> active_probes_;
    std::unordered_map<TlSinkId, TlSourceId> sinks_;
};

}  // namespace simrv::memory
