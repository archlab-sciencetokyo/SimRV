/** @file TileLinkProtocolChecker.hpp */
#pragma once

#include <expected>
#include <string>

#include "simrv/memory/TileLinkProtocol.hpp"
#include "simrv/util/SmallFlatMap.hpp"

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
    simrv::util::SmallFlatMap<TlSourceId, TlChannelA, 64> sources_;
    simrv::util::SmallFlatMap<TlSourceId, TlChannelC, 64> releases_;
    simrv::util::SmallFlatSet<Address, 64> active_probes_;
    simrv::util::SmallFlatMap<TlSinkId, TlSourceId, 64> sinks_;
};

}  // namespace simrv::memory
