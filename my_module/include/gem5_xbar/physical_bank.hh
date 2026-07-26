#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include "mem/port.hh"
#include "sim/eventq.hh"
#include "sim/sim_object.hh"

#include "params/PhysicalBank.hh"

namespace gem5::customxbar
{

/**
 * A bandwidth-aware physical-bank front end. It serializes requests according
 * to a configurable beat width and forwards them to an existing memory
 * controller or memory object.
 */
class PhysicalBank : public SimObject
{
  public:
    explicit PhysicalBank(const PhysicalBankParams &p);

    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

  private:
    class UpstreamPort : public ResponsePort
    {
      public:
        UpstreamPort(const std::string &name, PhysicalBank &owner)
            : ResponsePort(name), owner(owner)
        {
        }

        AddrRangeList getAddrRanges() const override;

      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;

      private:
        PhysicalBank &owner;
    };

    class DownstreamPort : public RequestPort
    {
      public:
        DownstreamPort(const std::string &name, PhysicalBank &owner)
            : RequestPort(name), owner(owner)
        {
        }

      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override;

      private:
        PhysicalBank &owner;
    };

    bool acceptRequest(PacketPtr pkt);
    bool acceptResponse(PacketPtr pkt);
    Tick forwardAtomic(PacketPtr pkt);
    void forwardFunctional(PacketPtr pkt);
    void retryRequest();
    void retryResponse();
    void processRequest();
    void processResponse();
    void scheduleFrontRequest(Tick delay);
    void trySendRequestRetry();
    void trySendResponseRetry();
    void validateDestination(PacketPtr pkt) const;
    Tick serviceLatency(PacketPtr pkt) const;

    UpstreamPort upstreamPort;
    DownstreamPort downstreamPort;
    std::deque<PacketPtr> requestQueue;
    std::deque<PacketPtr> responseQueue;
    EventFunctionWrapper requestEvent;
    EventFunctionWrapper responseEvent;

    const std::uint32_t bankId;
    const std::size_t dataWidthBits;
    const std::size_t beatBytes;
    const std::size_t requestBufferDepth;
    const std::size_t responseBufferDepth;
    const Tick baseLatency;
    const Tick perBeatLatency;

    bool waitingRequestRetry = false;
    bool waitingResponseRetry = false;
    bool needRequestRetry = false;
    bool needResponseRetry = false;
};

} // namespace gem5::customxbar
