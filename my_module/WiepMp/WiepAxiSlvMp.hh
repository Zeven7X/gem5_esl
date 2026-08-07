#ifndef WIEP_AXI_SLV_MP_HH
#define WIEP_AXI_SLV_MP_HH

#include "WiepAxiMpCommon.hh"

namespace WiepMp
{

// Cross-process AXI slave endpoint. AW/W/AR are read from shared memory and
// B/R are written directly to shared memory.
template <class C_OWNER, typename WirePacket, typename Codec,
          std::uint32_t Capacity = 256>
class WiepAxiSlvMp
{
    typedef WiepAxiSharedFifo<WirePacket, Codec, Capacity> SharedFifo;

  public:
    WiepAxiSlvMp(C_OWNER *owner, const WiepAxiMpConfig &config, Codec &codec)
        : m_awFifo(new SharedFifo(config, AxiChannel::AW, codec)),
          m_wFifo(new SharedFifo(config, AxiChannel::W, codec)),
          m_bFifo(new SharedFifo(config, AxiChannel::B, codec)),
          m_arFifo(new SharedFifo(config, AxiChannel::AR, codec)),
          m_rFifo(new SharedFifo(config, AxiChannel::R, codec)),
          m_obj(owner)
    {
    }

    ~WiepAxiSlvMp()
    {
        delete m_awFifo;
        delete m_wFifo;
        delete m_bFifo;
        delete m_arFifo;
        delete m_rFifo;
    }

    WiepAxiSlvMp(const WiepAxiSlvMp &) = delete;
    WiepAxiSlvMp &operator=(const WiepAxiSlvMp &) = delete;

    bool initialize()
    {
        return m_awFifo->initialize() && m_wFifo->initialize() &&
               m_bFifo->initialize() && m_arFifo->initialize() &&
               m_rFifo->initialize();
    }

    void onSyncReleased(std::uint64_t epoch)
    {
        setCurrentEpoch(epoch);

        if (m_awFifo->canPop())
            m_obj->popAwFunc();
        if (m_wFifo->canPop())
            m_obj->popWFunc();
        if (m_arFifo->canPop())
            m_obj->popArFunc();

        if (m_bFifo->consumeWriteRetry())
            m_obj->pushBFunc();
        if (m_rFifo->consumeWriteRetry())
            m_obj->pushRFunc();
    }

    SharedFifo *m_awFifo;
    SharedFifo *m_wFifo;
    SharedFifo *m_bFifo;
    SharedFifo *m_arFifo;
    SharedFifo *m_rFifo;

  private:
    void setCurrentEpoch(std::uint64_t epoch)
    {
        m_awFifo->setCurrentEpoch(epoch);
        m_wFifo->setCurrentEpoch(epoch);
        m_bFifo->setCurrentEpoch(epoch);
        m_arFifo->setCurrentEpoch(epoch);
        m_rFifo->setCurrentEpoch(epoch);
    }

    C_OWNER *m_obj;
};

} // namespace WiepMp

#endif // WIEP_AXI_SLV_MP_HH
