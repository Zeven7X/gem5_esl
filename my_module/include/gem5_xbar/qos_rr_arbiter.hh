#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "mem/port.hh"
#include "sim/eventq.hh"
#include "sim/sim_object.hh"

#include "params/QoSRoundRobinArbiter.hh"

namespace gem5::customxbar
{

/**
 * A programmable arbiter node. Python config code manually binds every
 * in_port/out_port, allowing arbitrary-width and arbitrary-depth fabrics.
 */
class QoSRoundRobinArbiter : public SimObject
{
  public:
    explicit QoSRoundRobinArbiter(const QoSRoundRobinArbiterParams &p);

    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

  private:
    struct Transit
    {
        PacketPtr pkt;
        PortID sourceInput;
    };

    class InputPort : public ResponsePort
    {
      public:
        InputPort(const std::string &name, QoSRoundRobinArbiter &owner,
                  PortID local_id)
            : ResponsePort(name, local_id), owner(owner), localId(local_id)
        {
        }

        AddrRangeList getAddrRanges() const override;

      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;

      private:
        QoSRoundRobinArbiter &owner;
        const PortID localId;
    };

    class OutputPort : public RequestPort
    {
      public:
        OutputPort(const std::string &name, QoSRoundRobinArbiter &owner,
                   PortID local_id, std::uint32_t global_id)
            : RequestPort(name, local_id), owner(owner), localId(local_id),
              globalId(global_id)
        {
        }

        std::uint32_t globalOutputId() const { return globalId; }

      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvReqRetry() override;
        void recvRangeChange() override;

      private:
        QoSRoundRobinArbiter &owner;
        const PortID localId;
        const std::uint32_t globalId;
    };

    bool acceptRequest(PortID input, PacketPtr pkt);
    bool acceptResponse(PortID output, PacketPtr pkt);
    Tick forwardAtomic(PacketPtr pkt);
    void forwardFunctional(PacketPtr pkt);
    void retryOutput(PortID output);
    void retryInputResponse(PortID input);
    void processArbitration();
    void processOutput(PortID output);
    void processResponse(PortID input);
    void scheduleArbitration(bool defer_to_next_tick = false);
    void scheduleOutput(PortID output, bool defer_to_next_tick = false);
    void scheduleResponse(PortID input);
    void trySendInputRetry(PortID input);
    void trySendOutputResponseRetry(PortID input);
    void sendRangeChange();

    PortID localOutputFor(PacketPtr pkt) const;
    PortID chooseWinner(PortID output);
    bool hasQueuedRequests() const;
    AddrRangeList getAddrRanges() const;

    std::vector<std::unique_ptr<InputPort>> inputPorts;
    std::vector<std::unique_ptr<OutputPort>> outputPorts;
    std::vector<std::deque<PacketPtr>> inputQueues;
    std::vector<std::deque<Transit>> outputQueues;
    std::vector<std::deque<PacketPtr>> responseQueues;
    std::vector<std::unordered_map<PacketPtr, PortID>> returnRoutes;
    std::vector<std::unique_ptr<EventFunctionWrapper>> outputEvents;
    std::vector<std::unique_ptr<EventFunctionWrapper>> responseEvents;
    std::vector<PortID> nextInput;
    std::vector<bool> waitingOutputRetry;
    std::vector<bool> waitingResponseRetry;
    std::vector<bool> needInputRetry;
    std::vector<bool> needOutputResponseRetry;
    std::vector<PortID> blockedResponseInput;
    EventFunctionWrapper arbitrationEvent;

    const std::size_t layerId;
    const std::uint32_t globalOutputBase;
    const std::size_t inputBufferDepth;
    const std::size_t outputBufferDepth;
    const std::size_t responseBufferDepth;
    const Tick arbitrationLatency;
    const Tick transferLatency;
};

} // namespace gem5::customxbar
