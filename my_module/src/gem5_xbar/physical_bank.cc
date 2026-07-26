#include "gem5_xbar/physical_bank.hh"

#include <algorithm>

#include "base/logging.hh"
#include "gem5_xbar/route_plan.hh"

namespace gem5::customxbar
{

PhysicalBank::PhysicalBank(const PhysicalBankParams &p)
    : SimObject(p),
      upstreamPort(name() + ".upstream", *this),
      downstreamPort(name() + ".downstream", *this),
      requestEvent([this] { processRequest(); }, name() + ".request_event"),
      responseEvent([this] { processResponse(); }, name() + ".response_event"),
      bankId(p.bank_id),
      dataWidthBits(p.data_width_bits),
      beatBytes(p.data_width_bits / 8),
      requestBufferDepth(p.request_buffer_depth),
      responseBufferDepth(p.response_buffer_depth),
      baseLatency(p.base_latency),
      perBeatLatency(p.per_beat_latency)
{
    panic_if(dataWidthBits == 0 || dataWidthBits % 8 != 0,
             "%s data_width_bits must be a non-zero multiple of 8", name());
    panic_if(requestBufferDepth == 0 || responseBufferDepth == 0,
             "%s queue depths must be non-zero", name());
}

Port &
PhysicalBank::getPort(const std::string &if_name, PortID idx)
{
    panic_if(idx != InvalidPortID, "%s does not use vector ports", name());

    if (if_name == "upstream")
        return upstreamPort;
    if (if_name == "downstream")
        return downstreamPort;

    return SimObject::getPort(if_name, idx);
}

AddrRangeList
PhysicalBank::UpstreamPort::getAddrRanges() const
{
    return owner.downstreamPort.getAddrRanges();
}

Tick
PhysicalBank::UpstreamPort::recvAtomic(PacketPtr pkt)
{
    return owner.forwardAtomic(pkt);
}

void
PhysicalBank::UpstreamPort::recvFunctional(PacketPtr pkt)
{
    owner.forwardFunctional(pkt);
}

bool
PhysicalBank::UpstreamPort::recvTimingReq(PacketPtr pkt)
{
    return owner.acceptRequest(pkt);
}

void
PhysicalBank::UpstreamPort::recvRespRetry()
{
    owner.retryResponse();
}

bool
PhysicalBank::DownstreamPort::recvTimingResp(PacketPtr pkt)
{
    return owner.acceptResponse(pkt);
}

void
PhysicalBank::DownstreamPort::recvReqRetry()
{
    owner.retryRequest();
}

void
PhysicalBank::DownstreamPort::recvRangeChange()
{
    owner.upstreamPort.sendRangeChange();
}

bool
PhysicalBank::acceptRequest(PacketPtr pkt)
{
    validateDestination(pkt);
    if (requestQueue.size() >= requestBufferDepth) {
        needRequestRetry = true;
        return false;
    }

    const bool was_empty = requestQueue.empty();
    requestQueue.push_back(pkt);
    if (was_empty && !waitingRequestRetry)
        scheduleFrontRequest(serviceLatency(pkt));
    return true;
}

bool
PhysicalBank::acceptResponse(PacketPtr pkt)
{
    if (responseQueue.size() >= responseBufferDepth) {
        needResponseRetry = true;
        return false;
    }

    responseQueue.push_back(pkt);
    if (!waitingResponseRetry && !responseEvent.scheduled())
        schedule(responseEvent, curTick());
    return true;
}

Tick
PhysicalBank::forwardAtomic(PacketPtr pkt)
{
    validateDestination(pkt);
    return serviceLatency(pkt) + downstreamPort.sendAtomic(pkt);
}

void
PhysicalBank::forwardFunctional(PacketPtr pkt)
{
    validateDestination(pkt);
    downstreamPort.sendFunctional(pkt);
}

void
PhysicalBank::retryRequest()
{
    waitingRequestRetry = false;
    if (!requestQueue.empty())
        scheduleFrontRequest(0);
}

void
PhysicalBank::retryResponse()
{
    waitingResponseRetry = false;
    if (!responseQueue.empty() && !responseEvent.scheduled())
        schedule(responseEvent, curTick());
}

void
PhysicalBank::processRequest()
{
    if (waitingRequestRetry || requestQueue.empty())
        return;

    PacketPtr pkt = requestQueue.front();
    if (downstreamPort.sendTimingReq(pkt)) {
        requestQueue.pop_front();
        trySendRequestRetry();
        if (!requestQueue.empty())
            scheduleFrontRequest(serviceLatency(requestQueue.front()));
    } else {
        waitingRequestRetry = true;
    }
}

void
PhysicalBank::processResponse()
{
    if (waitingResponseRetry || responseQueue.empty())
        return;

    PacketPtr pkt = responseQueue.front();
    if (upstreamPort.sendTimingResp(pkt)) {
        responseQueue.pop_front();
        trySendResponseRetry();
        if (!responseQueue.empty())
            schedule(responseEvent, curTick());
    } else {
        waitingResponseRetry = true;
    }
}

void
PhysicalBank::scheduleFrontRequest(Tick delay)
{
    if (!waitingRequestRetry && !requestEvent.scheduled() &&
        !requestQueue.empty()) {
        schedule(requestEvent, curTick() + delay);
    }
}

void
PhysicalBank::trySendRequestRetry()
{
    if (needRequestRetry && requestQueue.size() < requestBufferDepth) {
        needRequestRetry = false;
        upstreamPort.sendRetryReq();
    }
}

void
PhysicalBank::trySendResponseRetry()
{
    if (needResponseRetry && responseQueue.size() < responseBufferDepth) {
        needResponseRetry = false;
        downstreamPort.sendRetryResp();
    }
}

void
PhysicalBank::validateDestination(PacketPtr pkt) const
{
    const auto plan = pkt->getExtension<RoutePlan>();
    if (plan && plan->hasBankMapping()) {
        panic_if(plan->physicalBankId() != bankId,
                 "%s is bank %u but packet targets physical bank %u", name(),
                 bankId, plan->physicalBankId());
    }
}

Tick
PhysicalBank::serviceLatency(PacketPtr pkt) const
{
    const std::size_t bytes = std::max<std::size_t>(pkt->getSize(), 1);
    const std::size_t beats = (bytes + beatBytes - 1) / beatBytes;
    return baseLatency + beats * perBeatLatency;
}

} // namespace gem5::customxbar
