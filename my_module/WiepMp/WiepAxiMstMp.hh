//
// Created by ZevenKK on 2026/8/7.
//

#ifndef WIEP_AXI_MST_MP_HH
#define WIEP_AXI_MST_MP_HH

#include "WiepAxiMpCommon.hh"

struct AXITrfPack
{
    AXITrfPack(uint32_t _debug, uint32_t _wrOuts, uint32_t _rdOuts)
        : debugLevel(_debug), wrOuts(_wrOuts), rdOuts(_rdOuts)
    {
    }

    AXITrfPack() : debugLevel(0), wrOuts(0), rdOuts(0) {}

    uint32_t debugLevel;
    uint32_t wrOuts;
    uint32_t rdOuts;
};

namespace wiep_tlm_common
{

template <class C_OWNER>
class WiepAxiMstMp : public WiepMasterPort
{
    typedef WiepMp::WiepAxiMpChannel<WiepMp::WiepAxiAwMpPacket> MpAwChannel;
    typedef WiepMp::WiepAxiMpChannel<WiepMp::WiepAxiWMpPacket> MpWChannel;
    typedef WiepMp::WiepAxiMpChannel<WiepMp::WiepAxiBMpPacket> MpBChannel;
    typedef WiepMp::WiepAxiMpChannel<WiepMp::WiepAxiArMpPacket> MpArChannel;
    typedef WiepMp::WiepAxiMpChannel<WiepMp::WiepAxiRMpPacket> MpRChannel;
    typedef WiepMp::WiepAxiMpChannel<
        WiepMp::WiepAxiMstReadyMpPacket> MpReadyTxFifo;
    typedef WiepMp::WiepAxiMpChannel<
        WiepMp::WiepAxiSlvReadyMpPacket> MpReadyRxFifo;

  public:
    struct Para : public WiepModBaseParams
    {
        Para() { init(); }
        void init() {}
    };

    C_OWNER *m_obj;
    Para *para;

    void schedule(Event &event, Tick when) { m_obj->schedule(&event, when); }
    void schedule(Event *event, Tick when) { m_obj->schedule(event, when); }

    void pushAwFunc() { m_obj->pushAwFunc(); }
    void pushWFunc() { m_obj->pushWFunc(); }
    void pushArFunc() { m_obj->pushArFunc(); }
    void popRFunc() { m_obj->popRFunc(); }
    void popBFunc() { m_obj->popBFunc(); }

    void popAwFunc()
    {
        if (m_wrOstCnt >= m_wrOuts || !m_awFifo->canPop()) {
            if (m_wrOstCnt == m_wrOuts)
                DBG_VERB << "aw outstanding full." << endl;
            else
                DBG_VERB << "aw fifo cannot pop." << endl;
            return;
        }

        PacketPtr pAwTrf = m_awFifo->nbGet();
        pAwTrf->wiepReq->setChanType(wiep_tlm_common::AW);
        if (!sendTimingReq(pAwTrf))
            return;

        if (m_awChannelPrintCsvEn)
            m_obj->RecordCSV(wiep_tlm_common::AW, pAwTrf);

        DBG_TRAN << "Send AW transfer: " << pAwTrf->wiepReq->summary() << endl;
        ++m_wrOstCnt;
        m_totlWrOuts += m_wrOstCnt;
        m_wrDataBeatAllowed += pAwTrf->wiepReq->getBurstLength() + 1;
        m_awSendTime = curTick();

        if (m_wrDataBeatAllowed == pAwTrf->wiepReq->getBurstLength() + 1) {
            WiepTrigger<WiepAxiMstMp, &WiepAxiMstMp::popWFunc> trig(
                this, m_obj->clockPeriod());
        }

        m_awFifo->delTrf();
        if (m_perfEn) {
            if (m_wrOstCnt == 1)
                m_wr_time = curTick() / m_obj->clockPeriod();
            m_mapKTagVLat[pAwTrf->wiepReq->getTag()] =
                curTick() / m_obj->clockPeriod();
            m_writeOstdDist.sample(m_wrOstCnt);
            ++m_writeReqNum;
            m_writeDatNum += pAwTrf->wiepReq->getTotalSize();
        }
    }

    void popWFunc()
    {
        if (!m_wFifo->canPop())
            return;

        PacketPtr pWTrf = m_wFifo->nbGet();
        pWTrf->wiepReq->setChanType(wiep_tlm_common::W);
        if (m_wrDataBeatAllowed == 0) {
            DBG_VERB << "AW not sent, w pend!" << pWTrf->wiepReq->summary()
                     << endl;
            WiepTrigger<WiepAxiMstMp, &WiepAxiMstMp::popWFunc> trig(
                this, m_obj->clockPeriod());
            return;
        }

        if (m_awSendTime == curTick() &&
            pWTrf->wiepReq->getBurstLength() == m_wrDataBeatAllowed) {
            DBG_VERB << "AW is sent this cycle.W must be pended for 1 cycle!"
                     << endl;
            WiepTrigger<WiepAxiMstMp, &WiepAxiMstMp::popWFunc> trig(
                this, m_obj->clockPeriod());
            return;
        }

        if (!sendTimingReq(pWTrf))
            return;

        if (m_wChannelPrintCsvEn)
            m_obj->RecordCSV(wiep_tlm_common::W, pWTrf);
        --m_wrDataBeatAllowed;
        ++m_wrDataCnt;
        if (m_perfEn)
            ++m_writeBeatNum;
        DBG_TRAN << "Send W transfer: " << pWTrf->wiepReq->summary() << endl;
        m_wFifo->delTrf();
    }

    void popArFunc()
    {
        if (m_rdOstCnt >= m_rdOuts || !m_arFifo->canPop()) {
            if (m_rdOstCnt == m_rdOuts)
                DBG_VERB << "ar outstanding full." << endl;
            else
                DBG_VERB << "ar fifo cannot pop." << endl;
            return;
        }

        PacketPtr pArTrf = m_arFifo->nbGet();
        pArTrf->wiepReq->setChanType(wiep_tlm_common::AR);
        if (!sendTimingReq(pArTrf))
            return;

        if (m_arChannelPrintCsvEn)
            m_obj->RecordCSV(wiep_tlm_common::AR, pArTrf);
        DBG_TRAN << "Send AR transfer: " << pArTrf->wiepReq->summary() << endl;
        ++m_rdOstCnt;
        m_totlRdOuts += m_rdOstCnt;
        m_arFifo->delTrf();
        m_mapKTagVBeatcnt[pArTrf->wiepReq->getTag()] =
            pArTrf->wiepReq->getBurstLength() + 1;

        if (m_perfEn) {
            if (m_rdOstCnt == 1)
                m_rd_time = curTick() / m_obj->clockPeriod();
            m_mapKTagVLat[pArTrf->wiepReq->getTag()] =
                curTick() / m_obj->clockPeriod();
            m_mapKTagV1stBeatLat[pArTrf->wiepReq->getTag()] =
                curTick() / m_obj->clockPeriod();
            m_readOstdDist.sample(m_rdOstCnt);
            ++m_readReqNum;
        }
    }

    void retryBFunc()
    {
        if (m_brspWait) {
            m_brspWait = false;
            sendWiepRetry(m_bRetryTrf);
            sendRetry();
        }
    }

    void retryRFunc()
    {
        if (m_rrspWait) {
            m_rrspWait = false;
            sendWiepRetry(m_rRetryTrf);
            sendRetry();
        }
    }

    WiepTlmFifo<PacketPtr, WiepAxiMstMp<C_OWNER>,
                &WiepAxiMstMp<C_OWNER>::popAwFunc,
                &WiepAxiMstMp<C_OWNER>::pushAwFunc> *m_awFifo;
    WiepTlmFifo<PacketPtr, WiepAxiMstMp<C_OWNER>,
                &WiepAxiMstMp<C_OWNER>::popWFunc,
                &WiepAxiMstMp<C_OWNER>::pushWFunc> *m_wFifo;
    WiepTlmFifo<PacketPtr, WiepAxiMstMp<C_OWNER>,
                &WiepAxiMstMp<C_OWNER>::popArFunc,
                &WiepAxiMstMp<C_OWNER>::pushArFunc> *m_arFifo;
    WiepTlmFifo<PacketPtr, WiepAxiMstMp<C_OWNER>,
                &WiepAxiMstMp<C_OWNER>::popBFunc,
                &WiepAxiMstMp<C_OWNER>::retryBFunc> *m_bFifo;
    WiepTlmFifo<PacketPtr, WiepAxiMstMp<C_OWNER>,
                &WiepAxiMstMp<C_OWNER>::popRFunc,
                &WiepAxiMstMp<C_OWNER>::retryRFunc> *m_rFifo;

    PacketPtr m_bRetryTrf;
    PacketPtr m_rRetryTrf;

  private:
    bool m_brspWait;
    bool m_rrspWait;
    bool m_arChannelPrintCsvEn;
    bool m_awChannelPrintCsvEn;
    bool m_wChannelPrintCsvEn;
    uint32_t m_wrOuts;
    uint32_t m_rdOuts;
    uint32_t m_wrOstCnt;
    uint32_t m_rdOstCnt;
    std::map<uint64_t, uint32_t> m_mapKTagVBeatcnt;
    std::map<uint64_t, uint32_t> m_mapKTagVLat;
    std::map<uint64_t, uint32_t> m_mapKTagV1stBeatLat;
    uint64_t m_avWrLat;
    uint64_t m_avRdLat;
    uint64_t m_rdReqCnt;
    uint64_t m_wrReqCnt;
    uint64_t m_rdDataCnt;
    uint64_t m_wrDataCnt;
    uint32_t m_wrDataBeatAllowed;
    uint64_t m_awSendTime;
    uint64_t m_wr_time;
    uint64_t m_wr_time_total;
    uint64_t m_rd_time;
    uint64_t m_rd_time_total;
    uint64_t m_totlRdOuts;
    uint64_t m_totlWrOuts;
    uint64_t m_dataWidth;

  public:
    uint32_t m_debugLevel;

    void recvTimingRespMp()
    {
        applyPendingReadyState();
        receiveSlvReadyState();

        if (m_bMpChannel != NULL && !m_bFifo->full()) {
            WiepMp::WiepAxiBMpPacket wire;
            if (m_bMpChannel->nbGet(wire) &&
                wire.visibleTick <= curTick()) {
                PacketPtr pkt = WiepMp::bMpToPacket(wire);
                pkt->wiepReq->setChanType(B);
                if (receiveB(pkt))
                    m_bMpChannel->delTrf();
            }
        }

        if (m_rMpChannel != NULL && !m_rFifo->full()) {
            WiepMp::WiepAxiRMpPacket wire;
            if (m_rMpChannel->nbGet(wire) &&
                wire.visibleTick <= curTick()) {
                PacketPtr pkt = WiepMp::rMpToPacket(wire);
                pkt->wiepReq->setChanType(R);
                if (receiveR(pkt))
                    m_rMpChannel->delTrf();
            }
        }

        updateMstReadyState();

        // A full shared channel leaves the local FIFO head pending.
        popAwFunc();
        popWFunc();
        popArFunc();
        schedule(m_recvTimingRespEvent,
                 curTick() + m_obj->clockPeriod());
    }

    MpAwChannel *m_awMpChannel;
    MpWChannel *m_wMpChannel;
    MpBChannel *m_bMpChannel;
    MpArChannel *m_arMpChannel;
    MpRChannel *m_rMpChannel;
    MpReadyTxFifo *m_readyTxMpFifo;
    MpReadyRxFifo *m_readyRxMpFifo;
    EventWrapper<WiepAxiMstMp,
                 &WiepAxiMstMp::recvTimingRespMp> m_recvTimingRespEvent;

    bool m_awReady;
    bool m_wReady;
    bool m_arReady;
    bool m_pendingAwReady;
    bool m_pendingWReady;
    bool m_pendingArReady;
    bool m_pendingReadyValid;
    Tick m_pendingReadyApplyTick;
    bool m_lastRReady;
    bool m_lastBReady;

    WiepAxiMstMp(const wiep_module_name &_name, MemObject *_owner,
                   const AXITrfPack *_pack, PortID _id = InvalidPortID,
                   bool _bindWiep = true,
                   const WiepMp::WiepAxiMpConfig *_mpConfig = NULL)
        : WiepMasterPort(_name, _owner, _id, _bindWiep),
          m_obj(dynamic_cast<C_OWNER *>(_owner)), m_rRetryTrf(NULL),
          m_bRetryTrf(NULL), m_brspWait(false), m_rrspWait(false),
          m_wrOuts(_pack->wrOuts), m_rdOuts(_pack->rdOuts),
          m_debugLevel(_pack->debugLevel), m_wrOstCnt(0), m_rdOstCnt(0),
          m_avWrLat(0), m_avRdLat(0), m_rdReqCnt(0), m_wrReqCnt(0),
          m_wrDataCnt(0), m_rdDataCnt(0), m_wr_time_total(0),
          m_rd_time_total(0), m_totlRdOuts(0), m_totlWrOuts(0),
          m_dataWidth(256), m_wrDataBeatAllowed(0), m_awSendTime(0),
          m_arChannelPrintCsvEn(false), m_awChannelPrintCsvEn(false),
          m_wChannelPrintCsvEn(false), m_awMpChannel(NULL),
          m_wMpChannel(NULL), m_bMpChannel(NULL), m_arMpChannel(NULL),
          m_rMpChannel(NULL), m_readyTxMpFifo(NULL),
          m_readyRxMpFifo(NULL), m_recvTimingRespEvent(this),
          m_awReady(true), m_wReady(true), m_arReady(true),
          m_pendingAwReady(true), m_pendingWReady(true),
          m_pendingArReady(true), m_pendingReadyValid(false),
          m_pendingReadyApplyTick(0), m_lastRReady(true),
          m_lastBReady(true)
    {
        para = new Para();
        para->m_debugLevel = m_debugLevel;
        m_awFifo = new WiepTlmFifo<PacketPtr, WiepAxiMstMp<C_OWNER>,
            &WiepAxiMstMp<C_OWNER>::popAwFunc,
            &WiepAxiMstMp<C_OWNER>::pushAwFunc>("m_awfifo", this, 16, 0, 0);
        m_wFifo = new WiepTlmFifo<PacketPtr, WiepAxiMstMp<C_OWNER>,
            &WiepAxiMstMp<C_OWNER>::popWFunc,
            &WiepAxiMstMp<C_OWNER>::pushWFunc>("m_wfifo", this, 16 * 16, 0, 0);
        m_arFifo = new WiepTlmFifo<PacketPtr, WiepAxiMstMp<C_OWNER>,
            &WiepAxiMstMp<C_OWNER>::popArFunc,
            &WiepAxiMstMp<C_OWNER>::pushArFunc>("m_arfifo", this, 16, 0, 0);
        m_bFifo = new WiepTlmFifo<PacketPtr, WiepAxiMstMp<C_OWNER>,
            &WiepAxiMstMp<C_OWNER>::popBFunc,
            &WiepAxiMstMp<C_OWNER>::retryBFunc>("m_bfifo", this, 16, 1, 0);
        m_rFifo = new WiepTlmFifo<PacketPtr, WiepAxiMstMp<C_OWNER>,
            &WiepAxiMstMp<C_OWNER>::popRFunc,
            &WiepAxiMstMp<C_OWNER>::retryRFunc>("m_rfifo", this, 16 * 16, 1, 0);

        m_rRetryTrf = getPoolPkt();
        m_rRetryTrf->wiepReq->setChanType(R);
        m_bRetryTrf = getPoolPkt();
        m_bRetryTrf->wiepReq->setChanType(B);

        if (_mpConfig != NULL) {
            initializeMpChannels(*_mpConfig);
            schedule(m_recvTimingRespEvent,
                     curTick() + m_obj->clockPeriod());
        }
    }

    virtual ~WiepAxiMstMp()
    {
        double wrLat = 0.0;
        double rdLat = 0.0;
        if (m_wrReqCnt != 0)
            wrLat = 1.0 * m_avWrLat / m_wrReqCnt;
        if (m_rdReqCnt != 0)
            rdLat = 1.0 * m_avRdLat / m_rdReqCnt;
        if (m_wrReqCnt && m_wr_time != 0)
            m_wr_time_total += curTick() / m_obj->clockPeriod() - m_wr_time;
        if (m_rdReqCnt && m_rd_time != 0)
            m_rd_time_total += curTick() / m_obj->clockPeriod() - m_rd_time;

        delete m_awMpChannel;
        delete m_wMpChannel;
        delete m_bMpChannel;
        delete m_arMpChannel;
        delete m_rMpChannel;
        delete m_readyTxMpFifo;
        delete m_readyRxMpFifo;
    }

    AddrRangeList getAddrRanges() const {}

  protected:
    bool sendTimingReq(PacketPtr pkt)
    {
        applyPendingReadyState();
        const Tick visibleTick = curTick() + m_obj->clockPeriod();

        if (pkt->wiepReq->getChanType() == AW && m_awMpChannel != NULL) {
            if (!m_awReady)
                return false;
            WiepMp::WiepAxiAwMpPacket wire = WiepMp::packetToAwMp(pkt);
            wire.visibleTick = visibleTick;
            return m_awMpChannel->nbWrite(wire);
        }
        if (pkt->wiepReq->getChanType() == W && m_wMpChannel != NULL) {
            if (!m_wReady)
                return false;
            WiepMp::WiepAxiWMpPacket wire = WiepMp::packetToWMp(pkt);
            wire.visibleTick = visibleTick;
            return m_wMpChannel->nbWrite(wire);
        }
        if (pkt->wiepReq->getChanType() == AR && m_arMpChannel != NULL) {
            if (!m_arReady)
                return false;
            WiepMp::WiepAxiArMpPacket wire = WiepMp::packetToArMp(pkt);
            wire.visibleTick = visibleTick;
            return m_arMpChannel->nbWrite(wire);
        }
        return false;
    }

    virtual bool recvTimingResp(PacketPtr pkt)
    {
        if (!m_bindWiep && pkt->wiepReq->getChanType() == B) {
        }

        if (pkt->wiepReq->getChanType() == B)
            return receiveB(pkt);
        if (pkt->wiepReq->getChanType() == R)
            return receiveR(pkt);

        WIEP_ASSERT(false);
    }

    virtual void recvRetry() override
    {
        if (m_retryChn == AW) {
            DBG_VERB << "retry AW transfer." << endl;
            popAwFunc();
        } else if (m_retryChn == W) {
            DBG_VERB << "retry W transfer." << endl;
            popWFunc();
        } else if (m_retryChn == AR) {
            DBG_VERB << "retry AR transfer." << endl;
            popArFunc();
        } else {
            WIEP_ASSERT(false);
        }
    }

  public:
    Tick sendAtomicReq(PacketPtr pkt) { return sendAtomic(pkt); }

  private:
    void applyPendingReadyState()
    {
        if (!m_pendingReadyValid ||
            curTick() < m_pendingReadyApplyTick) {
            return;
        }

        m_awReady = m_pendingAwReady;
        m_wReady = m_pendingWReady;
        m_arReady = m_pendingArReady;
        m_pendingReadyValid = false;
    }

    void receiveSlvReadyState()
    {
        if (m_readyRxMpFifo == NULL)
            return;

        WiepMp::WiepAxiSlvReadyMpPacket state = {};
        if (!m_readyRxMpFifo->nbRead(state))
            return;

        m_pendingAwReady = state.awReady;
        m_pendingWReady = state.wReady;
        m_pendingArReady = state.arReady;
        m_pendingReadyApplyTick =
            curTick() + m_obj->clockPeriod();
        m_pendingReadyValid = true;
    }

    void updateMstReadyState()
    {
        if (m_readyTxMpFifo == NULL)
            return;

        const bool rReady = !m_rFifo->full();
        const bool bReady = !m_bFifo->full();
        if (rReady == m_lastRReady && bReady == m_lastBReady)
            return;

        WiepMp::WiepAxiMstReadyMpPacket state = {};
        state.rReady = rReady;
        state.bReady = bReady;
        if (m_readyTxMpFifo->nbWrite(state)) {
            m_lastRReady = rReady;
            m_lastBReady = bReady;
        }
    }

    void initializeMpChannels(const WiepMp::WiepAxiMpConfig &config)
    {
        m_awMpChannel = new MpAwChannel(WiepMp::makeSharedChannelConfig(
            config, WiepMp::AxiChannel::AW));
        m_wMpChannel = new MpWChannel(WiepMp::makeSharedChannelConfig(
            config, WiepMp::AxiChannel::W));
        m_bMpChannel = new MpBChannel(WiepMp::makeSharedChannelConfig(
            config, WiepMp::AxiChannel::B));
        m_arMpChannel = new MpArChannel(WiepMp::makeSharedChannelConfig(
            config, WiepMp::AxiChannel::AR));
        m_rMpChannel = new MpRChannel(WiepMp::makeSharedChannelConfig(
            config, WiepMp::AxiChannel::R));
        m_readyTxMpFifo = new MpReadyTxFifo(
            WiepMp::makeSharedChannelConfig(
                config, WiepMp::AxiChannel::MstReady));
        m_readyRxMpFifo = new MpReadyRxFifo(
            WiepMp::makeSharedChannelConfig(
                config, WiepMp::AxiChannel::SlvReady));

        const bool initialized =
            m_awMpChannel->initialize() && m_wMpChannel->initialize() &&
            m_bMpChannel->initialize() && m_arMpChannel->initialize() &&
            m_rMpChannel->initialize() &&
            m_readyTxMpFifo->initialize() &&
            m_readyRxMpFifo->initialize();
        WIEP_ASSERT(initialized);
    }

    bool receiveB(PacketPtr pkt)
    {
        if (m_bFifo->full()) {
            DBG_VERB << "m_bFifo full." << endl;
            m_brspWait = true;
            return false;
        }

        m_bFifo->nbWrite(pkt);
        --m_wrOstCnt;
        if (m_wrOstCnt == m_wrOuts - 1) {
            WiepTrigger<WiepAxiMstMp, &WiepAxiMstMp::popAwFunc> trig(
                this, m_obj->clockPeriod());
        }

        if (m_perfEn) {
            if (m_wrOstCnt == 0) {
                m_wr_time_total += curTick() / m_obj->clockPeriod() - m_wr_time;
                m_wr_time = 0;
            }
            m_mapKTagVLat[pkt->wiepReq->getTag()] =
                curTick() / m_obj->clockPeriod() -
                m_mapKTagVLat[pkt->wiepReq->getTag()];
            m_writeDelayCycleDist.sample(
                static_cast<uint64_t>(m_mapKTagVLat[pkt->wiepReq->getTag()]));
            DBG_TRAN << "m_bFifo receive response with "
                      << pkt->wiepReq->summary() << ", latency = " << dec
                      << m_mapKTagVLat[pkt->wiepReq->getTag()] << " cycles." << endl;
        } else {
            DBG_TRAN << "m_bFifo receive response with "
                      << pkt->wiepReq->summary() << endl;
        }

        if (m_perfEn) {
            ++m_wrReqCnt;
            m_avWrLat += m_mapKTagVLat[pkt->wiepReq->getTag()];
            m_mapKTagVLat.erase(pkt->wiepReq->getTag());
        }
        return true;
    }

    bool receiveR(PacketPtr pkt)
    {
        if (m_rFifo->full()) {
            DBG_VERB << "m_rFifo full." << endl;
            m_rrspWait = true;
            return false;
        }

        m_rFifo->nbWrite(pkt);
        ++m_rdDataCnt;
        if (m_perfEn)
            ++m_readBeatNum;

        std::map<uint64_t, uint32_t>::iterator it =
            m_mapKTagVBeatcnt.find(pkt->wiepReq->getTag());
        if (it == m_mapKTagVBeatcnt.end()) {
            DBG_DUMP << "m_rFifo receive response without ar req "
                     << pkt->wiepReq->summary() << endl;
            WIEP_ASSERT(false);
        }

        if (--it->second == 0) {
            --m_rdOstCnt;
            if (m_perfEn) {
                if (m_rdOstCnt == 0) {
                    m_rd_time_total += curTick() / m_obj->clockPeriod() - m_rd_time;
                    m_rd_time = 0;
                }
                m_mapKTagVLat[pkt->wiepReq->getTag()] =
                    curTick() / m_obj->clockPeriod() -
                    m_mapKTagVLat[pkt->wiepReq->getTag()];
                DBG_TRAN << "m_rFifo receive response with "
                          << pkt->wiepReq->summary() << ", latency = " << dec
                          << m_mapKTagVLat[pkt->wiepReq->getTag()] << " cycles." << endl;
                if (pkt->wiepReq->getBurstLength() == 0) {
                    m_readFirstDelayCycleDist.sample(
                        static_cast<uint64_t>(m_mapKTagVLat[pkt->wiepReq->getTag()]));
                }
                m_readDelayCycleDist.sample(
                    static_cast<uint64_t>(m_mapKTagVLat[pkt->wiepReq->getTag()]));
                m_readDatNum += pkt->wiepReq->getTotalSize();
            } else {
                DBG_TRAN << "m_rFifo receive response with "
                          << pkt->wiepReq->summary() << endl;
            }

            m_mapKTagVBeatcnt.erase(it);
            if (m_rdOstCnt == m_rdOuts - 1) {
                WiepTrigger<WiepAxiMstMp, &WiepAxiMstMp::popArFunc> trig(
                    this, m_obj->clockPeriod());
            }
            if (m_perfEn) {
                ++m_rdReqCnt;
                m_avRdLat += m_mapKTagVLat[pkt->wiepReq->getTag()];
                m_mapKTagVLat.erase(pkt->wiepReq->getTag());
            }
        } else {
            if (m_perfEn && it->second == pkt->wiepReq->getBurstLength()) {
                m_mapKTagV1stBeatLat[pkt->wiepReq->getTag()] =
                    curTick() / m_obj->clockPeriod() -
                    m_mapKTagV1stBeatLat[pkt->wiepReq->getTag()];
                m_readFirstDelayCycleDist.sample(
                    static_cast<uint64_t>(m_mapKTagV1stBeatLat[pkt->wiepReq->getTag()]));
            }
            DBG_TRAN << "m_rFifo receive response with "
                      << pkt->wiepReq->summary() << endl;
        }
        return true;
    }
};

} // namespace wiep_tlm_common

#endif // WIEP_AXI_MST_MP_HH
