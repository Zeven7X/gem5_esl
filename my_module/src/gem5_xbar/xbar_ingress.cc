#include "gem5_xbar/xbar_ingress.hh"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/logging.hh"
#include "gem5_xbar/route_plan.hh"

namespace gem5::customxbar
{

XBarIngress::XBarIngress(const XBarIngressParams &p)
    : SimObject(p),
      upstreamPort(name() + ".upstream", *this),
      downstreamPort(name() + ".downstream", *this),
      requestEvent([this] { processRequest(); }, name() + ".request_event"),
      responseEvent([this] { processResponse(); }, name() + ".response_event"),
      defaultRoute(p.route_outputs.begin(), p.route_outputs.end()),
      ostdLimit(p.ostd_limit),
      bufferDepth(p.buffer_depth),
      forwardLatency(p.forward_latency)
{
    panic_if(ostdLimit == 0, "XBarIngress %s needs a non-zero OSTD limit", name());
    panic_if(bufferDepth == 0, "XBarIngress %s needs a non-zero buffer depth", name());
}

Port &
XBarIngress::getPort(const std::string &if_name, PortID idx)
{
    panic_if(idx != InvalidPortID, "%s does not use vector ports", name());

    if (if_name == "upstream")
        return upstreamPort;
    if (if_name == "downstream")
        return downstreamPort;

    return SimObject::getPort(if_name, idx);
}

void
XBarIngress::setRouteFunction(RouteFunction function)
{
    routeFunction = std::move(function);
}

AddrRangeList
XBarIngress::UpstreamPort::getAddrRanges() const
{
    return owner.downstreamPort.getAddrRanges();
}

Tick
XBarIngress::UpstreamPort::recvAtomic(PacketPtr pkt)
{
    return owner.forwardAtomic(pkt);
}

void
XBarIngress::UpstreamPort::recvFunctional(PacketPtr pkt)
{
    owner.forwardFunctional(pkt);
}

bool
XBarIngress::UpstreamPort::recvTimingReq(PacketPtr pkt)
{
    return owner.acceptRequest(pkt);
}

void
XBarIngress::UpstreamPort::recvRespRetry()
{
    owner.retryResponse();
}

bool
XBarIngress::DownstreamPort::recvTimingResp(PacketPtr pkt)
{
    return owner.acceptResponse(pkt);
}

void
XBarIngress::DownstreamPort::recvReqRetry()
{
    owner.retryRequest();
}

void
XBarIngress::DownstreamPort::recvRangeChange()
{
    owner.sendRangeChange();
}

bool
XBarIngress::acceptRequest(PacketPtr pkt)
{
    if (outstanding >= ostdLimit || requestQueue.size() >= bufferDepth) {
        needRequestRetry = true;
        return false;
    }

    stampRoutePlan(pkt);
    ++outstanding;
    requestQueue.push_back(pkt);

    if (!waitingRequestRetry && !requestEvent.scheduled())
        schedule(requestEvent, curTick() + forwardLatency);

    return true;
}

bool
XBarIngress::acceptResponse(PacketPtr pkt)
{
    if (responseQueue.size() >= bufferDepth) {
        return false;
    }

    responseQueue.push_back(pkt);
    if (!waitingResponseRetry && !responseEvent.scheduled())
        schedule(responseEvent, curTick() + forwardLatency);

    return true;
}

Tick
XBarIngress::forwardAtomic(PacketPtr pkt)
{
    stampRoutePlan(pkt);
    return downstreamPort.sendAtomic(pkt);
}

void
XBarIngress::forwardFunctional(PacketPtr pkt)
{
    stampRoutePlan(pkt);
    downstreamPort.sendFunctional(pkt);
}

void
XBarIngress::retryResponse()
{
    waitingResponseRetry = false;
    if (!responseQueue.empty() && !responseEvent.scheduled())
        schedule(responseEvent, curTick());
}

void
XBarIngress::retryRequest()
{
    waitingRequestRetry = false;
    if (!requestQueue.empty() && !requestEvent.scheduled())
        schedule(requestEvent, curTick());
}

void
XBarIngress::processRequest()
{
    if (waitingRequestRetry || requestQueue.empty())
        return;

    PacketPtr pkt = requestQueue.front();
    if (downstreamPort.sendTimingReq(pkt)) {
        requestQueue.pop_front();
        trySendRetry();
        if (!requestQueue.empty())
            schedule(requestEvent, curTick() + std::max<Tick>(forwardLatency, 1));
    } else {
        waitingRequestRetry = true;
    }
}

void
XBarIngress::processResponse()
{
    if (waitingResponseRetry || responseQueue.empty())
        return;

    PacketPtr pkt = responseQueue.front();
    if (upstreamPort.sendTimingResp(pkt)) {
        responseQueue.pop_front();
        panic_if(outstanding == 0,
                 "XBarIngress %s received an unmatched response", name());
        --outstanding;
        trySendRetry();
        if (!responseQueue.empty())
            schedule(responseEvent, curTick() + std::max<Tick>(forwardLatency, 1));
    } else {
        waitingResponseRetry = true;
    }
}

void
XBarIngress::stampRoutePlan(PacketPtr pkt)
{
    const std::vector<std::uint32_t> route = routeFunction
        ? routeFunction(pkt)
        : defaultRoute;
    pkt->setExtension(std::make_shared<RoutePlan>(route));
}

void
XBarIngress::sendRangeChange()
{
    upstreamPort.sendRangeChange();
}

void
XBarIngress::trySendRetry()
{
    if (needRequestRetry && outstanding < ostdLimit &&
        requestQueue.size() < bufferDepth) {
        needRequestRetry = false;
        upstreamPort.sendRetryReq();
    }
}

} // namespace gem5::customxbar
