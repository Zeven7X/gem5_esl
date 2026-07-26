#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "mem/port.hh"
#include "sim/eventq.hh"
#include "sim/sim_object.hh"

#include "params/XBarIngress.hh"

namespace gem5::customxbar
{

/**
 * Request entry point for a custom XBAR fabric. It enforces an OSTD limit,
 * stamps a route plan on each accepted packet, and forwards responses back to
 * its upstream RequestPort.
 */
class XBarIngress : public SimObject
{
  public:
    using RouteFunction = std::function<std::vector<std::uint32_t>(PacketPtr)>;

    explicit XBarIngress(const XBarIngressParams &p);

    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

    void setRouteFunction(RouteFunction function);

  private:
    class UpstreamPort : public ResponsePort
    {
      public:
        UpstreamPort(const std::string &name, XBarIngress &owner)
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
        XBarIngress &owner;
    };

    class DownstreamPort : public RequestPort
    {
      public:
        DownstreamPort(const std::string &name, XBarIngress &owner)
            : RequestPort(name), owner(owner)
        {
        }

      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override;

      private:
        XBarIngress &owner;
    };

    bool acceptRequest(PacketPtr pkt);
    bool acceptResponse(PacketPtr pkt);
    Tick forwardAtomic(PacketPtr pkt);
    void forwardFunctional(PacketPtr pkt);
    void retryResponse();
    void retryRequest();
    void processRequest();
    void processResponse();
    void stampRoutePlan(PacketPtr pkt);
    void sendRangeChange();
    void trySendRetry();

    UpstreamPort upstreamPort;
    DownstreamPort downstreamPort;
    std::deque<PacketPtr> requestQueue;
    std::deque<PacketPtr> responseQueue;
    EventFunctionWrapper requestEvent;
    EventFunctionWrapper responseEvent;
    RouteFunction routeFunction;
    std::vector<std::uint32_t> defaultRoute;
    const std::size_t ostdLimit;
    const std::size_t bufferDepth;
    const Tick forwardLatency;
    std::size_t outstanding = 0;
    bool waitingRequestRetry = false;
    bool waitingResponseRetry = false;
    bool needRequestRetry = false;
};

} // namespace gem5::customxbar
