#ifndef WIEP_AXI_MST_MP_HH
#define WIEP_AXI_MST_MP_HH

#include "WiepAxiMpCommon.hh"

namespace WiepMp
{

// Cross-process AXI master endpoint. The five public members are shared-memory
// FIFO facades rather than local WiepTlmFifo objects.
template <class C_OWNER, typename WirePacket, typename Codec,
          std::uint32_t Capacity = 256>
class WiepAxiMstMp
{
    typedef WiepAxiSharedFifo<WirePacket, Codec, Capacity> SharedFifo;

  public:
    WiepAxiMstMp(C_OWNER *owner, const WiepAxiMpConfig &config, Codec &codec)
        : m_awFifo(new SharedFifo(config, AxiChannel::AW, codec)),
          m_wFifo(new SharedFifo(config, AxiChannel::W, codec)),
          m_bFifo(new SharedFifo(config, AxiChannel::B, codec)),
          m_arFifo(new SharedFifo(config, AxiChannel::AR, codec)),
          m_rFifo(new SharedFifo(config, AxiChannel::R, codec)),
          m_obj(owner)
    {
    }

    ~WiepAxiMstMp()
    {
        delete m_awFifo;
        delete m_wFifo;
        delete m_bFifo;
        delete m_arFifo;
        delete m_rFifo;
    }

    WiepAxiMstMp(const WiepAxiMstMp &) = delete;
    WiepAxiMstMp &operator=(const WiepAxiMstMp &) = delete;

    bool initialize()
    {
        return m_awFifo->initialize() && m_wFifo->initialize() &&
               m_bFifo->initialize() && m_arFifo->initialize() &&
               m_rFifo->initialize();
    }

    // Called once by the process-level synchronization EventWrapper.
    void onSyncReleased(std::uint64_t epoch)
    {
        setCurrentEpoch(epoch);

        if (m_bFifo->canPop())
            m_obj->popBFunc();
        if (m_rFifo->canPop())
            m_obj->popRFunc();

        if (m_awFifo->consumeWriteRetry())
            m_obj->pushAwFunc();
        if (m_wFifo->consumeWriteRetry())
            m_obj->pushWFunc();
        if (m_arFifo->consumeWriteRetry())
            m_obj->pushArFunc();
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

#endif // WIEP_AXI_MST_MP_HH
