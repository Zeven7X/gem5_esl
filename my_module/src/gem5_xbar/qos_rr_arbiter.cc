#include "gem5_xbar/qos_rr_arbiter.hh"

#include <algorithm>
#include <limits>
#include <string>

#include "base/logging.hh"
#include "gem5_xbar/route_plan.hh"

namespace gem5::customxbar
{

QoSRoundRobinArbiter::QoSRoundRobinArbiter(
    const QoSRoundRobinArbiterParams &p)
    : SimObject(p),
      arbitrationEvent([this] { processArbitration(); },
                       name() + ".arbitration_event"),
      layerId(p.layer_id),
      globalOutputBase(p.global_output_base),
      inputBufferDepth(p.input_buffer_depth),
      outputBufferDepth(p.output_buffer_depth),
      responseBufferDepth(p.response_buffer_depth),
      arbitrationLatency(p.arbitration_latency),
      transferLatency(p.transfer_latency)
{
    panic_if(p.num_inputs == 0 || p.num_outputs == 0,
             "%s needs at least one input and one output", name());
    panic_if(inputBufferDepth == 0 || outputBufferDepth == 0 ||
             responseBufferDepth == 0,
             "%s queue depths must be non-zero", name());

    inputPorts.reserve(p.num_inputs);
    inputQueues.resize(p.num_inputs);
    responseQueues.resize(p.num_inputs);
    responseEvents.reserve(p.num_inputs);
    needInputRetry.assign(p.num_inputs, false);

    for (PortID input = 0; input < p.num_inputs; ++input) {
        inputPorts.emplace_back(std::make_unique<InputPort>(
            name() + ".in_ports[" + std::to_string(input) + "]",
            *this, input));
        responseEvents.emplace_back(std::make_unique<EventFunctionWrapper>(
            [this, input] { processResponse(input); },
            name() + ".response_event[" + std::to_string(input) + "]"));
    }

    outputPorts.reserve(p.num_outputs);
    outputQueues.resize(p.num_outputs);
    returnRoutes.resize(p.num_outputs);
    outputEvents.reserve(p.num_outputs);
    nextInput.assign(p.num_outputs, 0);
    waitingOutputRetry.assign(p.num_outputs, false);
    waitingResponseRetry.assign(p.num_inputs, false);

    for (PortID output = 0; output < p.num_outputs; ++output) {
        outputPorts.emplace_back(std::make_unique<OutputPort>(
            name() + ".out_ports[" + std::to_string(output) + "]",
            *this, output, globalOutputBase + output));
        outputEvents.emplace_back(std::make_unique<EventFunctionWrapper>(
            [this, output] { processOutput(output); },
            name() + ".output_event[" + std::to_string(output) + "]"));
    }
}

Port &
QoSRoundRobinArbiter::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "in_ports") {
        panic_if(idx == InvalidPortID || idx >= inputPorts.size(),
                 "%s invalid in_ports index", name());
        return *inputPorts[idx];
    }

    if (if_name == "out_ports") {
        panic_if(idx == InvalidPortID || idx >= outputPorts.size(),
                 "%s invalid out_ports index", name());
        return *outputPorts[idx];
    }

    return SimObject::getPort(if_name, idx);
}

AddrRangeList
QoSRoundRobinArbiter::InputPort::getAddrRanges() const
{
    return owner.getAddrRanges();
}

Tick
QoSRoundRobinArbiter::InputPort::recvAtomic(PacketPtr pkt)
{
    return owner.forwardAtomic(pkt);
}

void
QoSRoundRobinArbiter::InputPort::recvFunctional(PacketPtr pkt)
{
    owner.forwardFunctional(pkt);
}

bool
QoSRoundRobinArbiter::InputPort::recvTimingReq(PacketPtr pkt)
{
    return owner.acceptRequest(localId, pkt);
}

void
QoSRoundRobinArbiter::InputPort::recvRespRetry()
{
    owner.retryInputResponse(localId);
}

bool
QoSRoundRobinArbiter::OutputPort::recvTimingResp(PacketPtr pkt)
{
    return owner.acceptResponse(localId, pkt);
}

void
QoSRoundRobinArbiter::OutputPort::recvReqRetry()
{
    owner.retryOutput(localId);
}

void
QoSRoundRobinArbiter::OutputPort::recvRangeChange()
{
    owner.sendRangeChange();
}

bool
QoSRoundRobinArbiter::acceptRequest(PortID input, PacketPtr pkt)
{
    if (inputQueues[input].size() >= inputBufferDepth) {
        needInputRetry[input] = true;
        return false;
    }

    (void)localOutputFor(pkt);
    inputQueues[input].push_back(pkt);
    scheduleArbitration();
    return true;
}

bool
QoSRoundRobinArbiter::acceptResponse(PortID output, PacketPtr pkt)
{
    auto route = returnRoutes[output].find(pkt);
    panic_if(route == returnRoutes[output].end(),
             "%s received a response with no return route", name());

    const PortID input = route->second;
    if (responseQueues[input].size() >= responseBufferDepth)
        return false;

    returnRoutes[output].erase(route);
    responseQueues[input].push_back(pkt);
    scheduleResponse(input);
    return true;
}

Tick
QoSRoundRobinArbiter::forwardAtomic(PacketPtr pkt)
{
    return outputPorts[localOutputFor(pkt)]->sendAtomic(pkt);
}

void
QoSRoundRobinArbiter::forwardFunctional(PacketPtr pkt)
{
    outputPorts[localOutputFor(pkt)]->sendFunctional(pkt);
}

void
QoSRoundRobinArbiter::retryOutput(PortID output)
{
    waitingOutputRetry[output] = false;
    scheduleOutput(output);
}

void
QoSRoundRobinArbiter::retryInputResponse(PortID input)
{
    waitingResponseRetry[input] = false;
    scheduleResponse(input);
}

void
QoSRoundRobinArbiter::processArbitration()
{
    for (PortID output = 0; output < outputPorts.size(); ++output) {
        if (outputQueues[output].size() >= outputBufferDepth)
            continue;

        const PortID winner = chooseWinner(output);
        if (winner == InvalidPortID)
            continue;

        PacketPtr pkt = inputQueues[winner].front();
        inputQueues[winner].pop_front();
        outputQueues[output].push_back({pkt, winner});
        trySendInputRetry(winner);
        scheduleOutput(output);
    }

    if (hasQueuedRequests())
        scheduleArbitration(true);
}

void
QoSRoundRobinArbiter::processOutput(PortID output)
{
    if (waitingOutputRetry[output] || outputQueues[output].empty())
        return;

    const Transit &transit = outputQueues[output].front();
    const auto inserted = returnRoutes[output].emplace(
        transit.pkt, transit.sourceInput);
    panic_if(!inserted.second, "%s duplicated timing request", name());

    if (outputPorts[output]->sendTimingReq(transit.pkt)) {
        outputQueues[output].pop_front();
        if (!outputQueues[output].empty())
            scheduleOutput(output, true);
        scheduleArbitration();
    } else {
        returnRoutes[output].erase(transit.pkt);
        waitingOutputRetry[output] = true;
    }
}

void
QoSRoundRobinArbiter::processResponse(PortID input)
{
    if (waitingResponseRetry[input] || responseQueues[input].empty())
        return;

    PacketPtr pkt = responseQueues[input].front();
    if (inputPorts[input]->sendTimingResp(pkt)) {
        responseQueues[input].pop_front();
        if (!responseQueues[input].empty())
            scheduleResponse(input);
    } else {
        waitingResponseRetry[input] = true;
    }
}

void
QoSRoundRobinArbiter::scheduleArbitration(bool defer_to_next_tick)
{
    if (arbitrationEvent.scheduled())
        return;

    Tick when = curTick() + arbitrationLatency;
    if (defer_to_next_tick && when <= curTick())
        when = curTick() + 1;
    schedule(arbitrationEvent, when);
}

void
QoSRoundRobinArbiter::scheduleOutput(PortID output, bool defer_to_next_tick)
{
    if (waitingOutputRetry[output] || outputEvents[output]->scheduled() ||
        outputQueues[output].empty()) {
        return;
    }

    Tick when = curTick() + transferLatency;
    if (defer_to_next_tick && when <= curTick())
        when = curTick() + 1;
    schedule(*outputEvents[output], when);
}

void
QoSRoundRobinArbiter::scheduleResponse(PortID input)
{
    if (waitingResponseRetry[input] || responseEvents[input]->scheduled() ||
        responseQueues[input].empty()) {
        return;
    }

    schedule(*responseEvents[input], curTick());
}

void
QoSRoundRobinArbiter::trySendInputRetry(PortID input)
{
    if (needInputRetry[input] && inputQueues[input].size() < inputBufferDepth) {
        needInputRetry[input] = false;
        inputPorts[input]->sendRetryReq();
    }
}

void
QoSRoundRobinArbiter::sendRangeChange()
{
    for (const auto &port : inputPorts)
        port->sendRangeChange();
}

PortID
QoSRoundRobinArbiter::localOutputFor(PacketPtr pkt) const
{
    const auto plan = pkt->getExtension<RoutePlan>();
    panic_if(!plan, "%s received a packet with no XBAR route plan", name());

    const std::uint32_t global = plan->outputForLayer(layerId);
    panic_if(global < globalOutputBase ||
             global >= globalOutputBase + outputPorts.size(),
             "%s route selected global output %u outside [%u, %u)", name(),
             global, globalOutputBase,
             globalOutputBase + outputPorts.size());
    return static_cast<PortID>(global - globalOutputBase);
}

PortID
QoSRoundRobinArbiter::chooseWinner(PortID output)
{
    std::uint8_t bestQos = 0;
    bool found = false;

    for (PortID input = 0; input < inputPorts.size(); ++input) {
        if (inputQueues[input].empty() ||
            localOutputFor(inputQueues[input].front()) != output) {
            continue;
        }

        bestQos = found ? std::max(bestQos, inputQueues[input].front()->qosValue())
                        : inputQueues[input].front()->qosValue();
        found = true;
    }

    if (!found)
        return InvalidPortID;

    for (PortID distance = 0; distance < inputPorts.size(); ++distance) {
        const PortID input = (nextInput[output] + distance) % inputPorts.size();
        if (inputQueues[input].empty() ||
            localOutputFor(inputQueues[input].front()) != output ||
            inputQueues[input].front()->qosValue() != bestQos) {
            continue;
        }

        nextInput[output] = (input + 1) % inputPorts.size();
        return input;
    }

    panic("%s failed to select an arbitration winner", name());
}

bool
QoSRoundRobinArbiter::hasQueuedRequests() const
{
    return std::any_of(inputQueues.begin(), inputQueues.end(),
                       [](const auto &queue) { return !queue.empty(); });
}

AddrRangeList
QoSRoundRobinArbiter::getAddrRanges() const
{
    AddrRangeList ranges;
    for (const auto &port : outputPorts) {
        const AddrRangeList downstream_ranges = port->getAddrRanges();
        ranges.insert(ranges.end(), downstream_ranges.begin(),
                      downstream_ranges.end());
    }
    return ranges;
}

} // namespace gem5::customxbar
