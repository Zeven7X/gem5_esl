#ifndef WIEP_AXI_MST_MP_HH
#define WIEP_AXI_MST_MP_HH

#include "WiepAxiMpCommon.hh"
#include "../include/WiepTlmFifo.h"

namespace WiepMp
{

// Keeps the existing Host call form:
//     m_axiMst->m_arFifo->nbWrite(pkt)
template <class C_OWNER, typename WirePacket, typename Codec,
          std::uint32_t Capacity = 256>
class WiepAxiMstMp
    : public WiepAxiMpTransport<WirePacket, Codec, Capacity>
{
    typedef WiepAxiMpTransport<WirePacket, Codec, Capacity> Transport;

  public:
    void popAwFunc()
    {
        sendFront(m_awFifo, AxiChannel::AW, Transport::awChannel_);
    }
    void popWFunc()
    {
        sendFront(m_wFifo, AxiChannel::W, Transport::wChannel_);
    }
    void popArFunc()
    {
        sendFront(m_arFifo, AxiChannel::AR, Transport::arChannel_);
    }

    void pushAwFunc() { m_obj->pushAwFunc(); }
    void pushWFunc() { m_obj->pushWFunc(); }
    void pushArFunc() { m_obj->pushArFunc(); }
    void popBFunc() { m_obj->popBFunc(); }
    void popRFunc() { m_obj->popRFunc(); }
    void retryBFunc() { drainB(); }
    void retryRFunc() { drainR(); }

  private:
    typedef WiepTlmFifo<PacketPtr, WiepAxiMstMp,
                        &WiepAxiMstMp::popAwFunc,
                        &WiepAxiMstMp::pushAwFunc> AwFifo;
    typedef WiepTlmFifo<PacketPtr, WiepAxiMstMp,
                        &WiepAxiMstMp::popWFunc,
                        &WiepAxiMstMp::pushWFunc> WFifo;
    typedef WiepTlmFifo<PacketPtr, WiepAxiMstMp,
                        &WiepAxiMstMp::popArFunc,
                        &WiepAxiMstMp::pushArFunc> ArFifo;
    typedef WiepTlmFifo<PacketPtr, WiepAxiMstMp,
                        &WiepAxiMstMp::popBFunc,
                        &WiepAxiMstMp::retryBFunc> BFifo;
    typedef WiepTlmFifo<PacketPtr, WiepAxiMstMp,
                        &WiepAxiMstMp::popRFunc,
                        &WiepAxiMstMp::retryRFunc> RFifo;

  public:
    WiepAxiMstMp(C_OWNER *owner, const WiepAxiMpConfig &config, Codec &codec)
        : Transport(config, codec), m_obj(owner)
    {
        m_awFifo = new AwFifo("m_awfifo", this, 16, 0, 0);
        m_wFifo = new WFifo("m_wfifo", this, 256, 0, 0);
        m_arFifo = new ArFifo("m_arfifo", this, 16, 0, 0);
        m_bFifo = new BFifo("m_bfifo", this, 16, 1, 0);
        m_rFifo = new RFifo("m_rfifo", this, 256, 1, 0);
    }

    ~WiepAxiMstMp()
    {
        delete m_awFifo;
        delete m_wFifo;
        delete m_arFifo;
        delete m_bFifo;
        delete m_rFifo;
    }

    bool initialize() { return Transport::initializeTransport(); }

    // Call once after the process-wide epoch barrier is released.
    void onSyncReleased(std::uint64_t epoch)
    {
        Transport::setCurrentEpoch(epoch);
        drainResponses();
        flushRequests();
    }

    void schedule(Event &event, Tick when) { m_obj->schedule(&event, when); }
    void schedule(Event *event, Tick when) { m_obj->schedule(event, when); }

    AwFifo *m_awFifo;
    WFifo *m_wFifo;
    ArFifo *m_arFifo;
    BFifo *m_bFifo;
    RFifo *m_rFifo;

  private:
    template <typename Fifo>
    void sendFront(Fifo *fifo, AxiChannel axiChannel,
                   typename Transport::Channel &channel)
    {
        if (!fifo->canPop())
            return;

        PacketPtr packet = fifo->nbGet();
        if (Transport::sendPacket(packet, axiChannel, channel))
            fifo->delTrf();
        // Ring full: retain the packet and retry after the next barrier.
    }

    void drainB()
    {
        while (Transport::receivePacket(AxiChannel::B,
                                        Transport::bChannel_, m_bFifo)) {
        }
    }

    void drainR()
    {
        while (Transport::receivePacket(AxiChannel::R,
                                        Transport::rChannel_, m_rFifo)) {
        }
    }

    void drainResponses()
    {
        drainB();
        drainR();
    }

    void flushRequests()
    {
        popAwFunc();
        popWFunc();
        popArFunc();
    }

    C_OWNER *m_obj;
};

} // namespace WiepMp

#endif // WIEP_AXI_MST_MP_HH
