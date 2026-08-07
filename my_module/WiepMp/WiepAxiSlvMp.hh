#ifndef WIEP_AXI_SLV_MP_HH
#define WIEP_AXI_SLV_MP_HH

#include "WiepAxiMpCommon.hh"
#include "../include/WiepTlmFifo.h"

namespace WiepMp
{

template <class C_OWNER, typename WirePacket, typename Codec,
          std::uint32_t Capacity = 256>
class WiepAxiSlvMp
    : public WiepAxiMpTransport<WirePacket, Codec, Capacity>
{
    typedef WiepAxiMpTransport<WirePacket, Codec, Capacity> Transport;

  public:
    void popAwFunc() { m_obj->popAwFunc(); }
    void popWFunc() { m_obj->popWFunc(); }
    void popArFunc() { m_obj->popArFunc(); }
    void pushBFunc() { m_obj->pushBFunc(); }
    void pushRFunc() { m_obj->pushRFunc(); }

    void popBFunc()
    {
        sendFront(m_bFifo, AxiChannel::B, Transport::bChannel_);
    }
    void popRFunc()
    {
        sendFront(m_rFifo, AxiChannel::R, Transport::rChannel_);
    }

    void retryAwFunc() { drainAw(); }
    void retryWFunc() { drainW(); }
    void retryArFunc() { drainAr(); }

  private:
    typedef WiepTlmFifo<PacketPtr, WiepAxiSlvMp,
                        &WiepAxiSlvMp::popAwFunc,
                        &WiepAxiSlvMp::retryAwFunc> AwFifo;
    typedef WiepTlmFifo<PacketPtr, WiepAxiSlvMp,
                        &WiepAxiSlvMp::popWFunc,
                        &WiepAxiSlvMp::retryWFunc> WFifo;
    typedef WiepTlmFifo<PacketPtr, WiepAxiSlvMp,
                        &WiepAxiSlvMp::popArFunc,
                        &WiepAxiSlvMp::retryArFunc> ArFifo;
    typedef WiepTlmFifo<PacketPtr, WiepAxiSlvMp,
                        &WiepAxiSlvMp::popBFunc,
                        &WiepAxiSlvMp::pushBFunc> BFifo;
    typedef WiepTlmFifo<PacketPtr, WiepAxiSlvMp,
                        &WiepAxiSlvMp::popRFunc,
                        &WiepAxiSlvMp::pushRFunc> RFifo;

  public:
    WiepAxiSlvMp(C_OWNER *owner, const WiepAxiMpConfig &config, Codec &codec)
        : Transport(config, codec), m_obj(owner)
    {
        m_awFifo = new AwFifo("m_awfifo", this, 16, 0, 0);
        m_wFifo = new WFifo("m_wfifo", this, 256, 1, 0);
        m_arFifo = new ArFifo("m_arfifo", this, 16, 0, 0);
        m_bFifo = new BFifo("m_bfifo", this, 16, 2, 0);
        m_rFifo = new RFifo("m_rfifo", this, 256, 1, 0);
    }

    ~WiepAxiSlvMp()
    {
        delete m_awFifo;
        delete m_wFifo;
        delete m_arFifo;
        delete m_bFifo;
        delete m_rFifo;
    }

    bool initialize() { return Transport::initializeTransport(); }

    void onSyncReleased(std::uint64_t epoch)
    {
        Transport::setCurrentEpoch(epoch);
        drainRequests();
        flushResponses();
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
    }

    void drainAw()
    {
        while (Transport::receivePacket(AxiChannel::AW,
                                        Transport::awChannel_, m_awFifo)) {
        }
    }

    void drainW()
    {
        while (Transport::receivePacket(AxiChannel::W,
                                        Transport::wChannel_, m_wFifo)) {
        }
    }

    void drainAr()
    {
        while (Transport::receivePacket(AxiChannel::AR,
                                        Transport::arChannel_, m_arFifo)) {
        }
    }

    void drainRequests()
    {
        drainAw();
        drainW();
        drainAr();
    }

    void flushResponses()
    {
        popBFunc();
        popRFunc();
    }

    C_OWNER *m_obj;
};

} // namespace WiepMp

#endif // WIEP_AXI_SLV_MP_HH
