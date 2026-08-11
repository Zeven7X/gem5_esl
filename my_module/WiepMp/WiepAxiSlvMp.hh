//
// Created by ZevenKK on 2026 / 8 / 7.
//
#ifndef WIEP_AXI_SLV_MP_HH
#define WIEP_AXI_SLV_MP_HH

#include "WiepAxiMpCommon.hh"

namespace wiep_tlm_common
{
struct AXITrfPack;
template <class C_OWNER> class WiepAxiSlvMp : public WiepSlavePort
{
    typedef WiepMp::WiepAxiMpChannel<WiepMp::WiepAxiAwMpPacket> MpAwChannel;
    typedef WiepMp::WiepAxiMpChannel<WiepMp::WiepAxiWMpPacket> MpWChannel;
    typedef WiepMp::WiepAxiMpChannel<WiepMp::WiepAxiBMpPacket> MpBChannel;
    typedef WiepMp::WiepAxiMpChannel<WiepMp::WiepAxiArMpPacket> MpArChannel;
    typedef WiepMp::WiepAxiMpChannel<WiepMp::WiepAxiRMpPacket> MpRChannel;
    typedef WiepMp::WiepAxiMpChannel<WiepMp::WiepAxiSlvReadyMpPacket> MpReadyTxFifo;
    typedef WiepMp::WiepAxiMpChannel<WiepMp::WiepAxiMstReadyMpPacket> MpReadyRxFifo;

  public:
    struct Para : public WiepModBaseParams
    {
        Para()
        {
            init();
        }
        void init() {}
    };

  public:
    C_OWNER *m_obj;
    Para *para;
    void schedule(Event &event, Tick when)
    {
        m_obj->schedule(&event, when);
    }
    void schedule(Event *event, Tick when)
    {
        m_obj->schedule(event, when);
    }
    inline void popAwFunc()
    {
        m_obj->popAwFunc();
    }
    inline void popWFunc()
    {
        m_obj->popWFunc();
    }
    inline void popArFunc()
    {
        m_obj->popArFunc();
    }
    inline void pushRFunc()
    {
        m_obj->pushRFunc();
    }
    inline void pushBFunc()
    {
        m_obj->pushBFunc();
    }
    inline void retryAwFunc()
    {
        if (m_wrReqWait)
        {
            m_wrReqWait = false;
            sendWiepRetry(m_awRetryTrf);
            sendRetry();
        }
    }
    inline void retryWFunc()
    {
        if (m_wReqWait)
        {
            m_wReqWait = false;
            sendWiepRetry(m_wRetryTrf);
            sendRetry();
        }
    }
    inline void retryArFunc()
    {
        if (m_rdReqWait)
        {
            m_rdReqWait = false;
            sendWiepRetry(m_arRetryTrf);
            sendRetry();
        }
    }
    void popBFunc()
    {
        if (!m_bFifo->canPop())
            return;
        PacketPtr pBTrf = m_bFifo->nbGet();
        if (!pBTrf->isResponse())
        {
            pBTrf->makeResponse();
        }
        pBTrf->wiepReq->setChanType(wiep_tlm_common::B);
        if (sendTimingResp(pBTrf))
        {
            if (m_bChannelPrintCsvEn)
            {
                m_obj->RecordCSV(wiep_tlm_common::B, pBTrf);
            }
            DBG_TRAN << "Send B transfer: " << pBTrf->wiepReq->summary() << endl;
            m_bFifo->delTrf();
            m_wrOstCnt--;
            if (m_wrOstCnt == m_wrOuts - 1)
            {
                WiepTrigger<WiepAxiSlvMp, &WiepAxiSlvMp::retryAwFunc> trig(this);
            }
            map<uint64_t, double>::iterator m_it = m_reqTimeMap.find(pBTrf->wiepReq->getTag());
            WIEP_ASSERT(m_it != m_reqTimeMap.end());
            m_reqTimeMap.erase(m_it);
        }
    }
    void popRFunc()
    {
        if (!m_rFifo->canPop())
            return;
        PacketPtr pRTrf = m_rFifo->nbGet();
        if (!pRTrf->isResponse())
        {
            pRTrf->makeResponse();
        }
        pRTrf->wiepReq->setChanType(wiep_tlm_common::R);
        if (sendTimingResp(pRTrf))
        {
            DBG_TRAN << "Send R transfer: " << pRTrf->wiepReq->summary() << endl;
            if (m_rChannelPrintCsvEn)
            {
                m_obj->RecordCSV(wiep_tlm_common::R, pRTrf);
            }
            m_rFifo->delTrf();
            std::map<uint64_t, uint32_t>::iterator it =
                m_mapKTagVBeatcnt.find(pRTrf->wiepReq->getTag());
            if (it == m_mapKTagVBeatcnt.end())
            {
                DBG_DUMP << "Send R transfer not find : " << pRTrf->wiepReq->summary() << endl;
                WIEP_ASSERT(false);
            }
            if (--it->second == 0)
            {
                m_rdOstCnt--;
                if (m_rdOstCnt == m_rdOuts - 1)
                {
                    WiepTrigger<WiepAxiSlvMp, &WiepAxiSlvMp::retryArFunc> trig(this);
                }
                m_mapKTagVBeatcnt.erase(it);
                map<uint64_t, double>::iterator m_it = m_reqTimeMap.find(pRTrf->wiepReq->getTag());
                WIEP_ASSERT(m_it != m_reqTimeMap.end());
                m_reqTimeMap.erase(m_it);
            }
        }
    }
    inline void setAwFifoInfo(uint32_t _depth, uint32_t _delay)
    {
        m_awFifo->setFifoDepth(_depth);
        m_awFifo->setFifoDelay(_delay);
    }
    inline void setWFifoInfo(uint32_t _depth, uint32_t _delay)
    {
        m_wFifo->setFifoDepth(_depth);
        m_wFifo->setFifoDelay(_delay);
    }
    inline void setArFifoInfo(uint32_t _depth, uint32_t _delay)
    {
        m_arFifo->setFifoDepth(_depth);
        m_arFifo->setFifoDelay(_delay);
    }
    inline void setBFifoInfo(uint32_t _depth, uint32_t _delay)
    {
        m_bFifo->setFifoDepth(_depth);
        m_bFifo->setFifoDelay(_delay);
    }
    inline void setRFifoInfo(uint32_t _depth, uint32_t _delay)
    {
        m_rFifo->setFifoDepth(_depth);
        m_rFifo->setFifoDelay(_delay);
    }
    inline void setPeriod(const Tick &_period)
    {
        m_awFifo->setPeriod(_period);
        m_wFifo->setPeriod(_period);
        m_arFifo->setPeriod(_period);
        m_rFifo->setPeriod(_period);
        m_bFifo->setPeriod(_period);
    }
    WiepTlmFifo<PacketPtr, WiepAxiSlvMp<C_OWNER>, &WiepAxiSlvMp<C_OWNER>::popAwFunc,
                &WiepAxiSlvMp<C_OWNER>::retryAwFunc> *m_awFifo;
    WiepTlmFifo<PacketPtr, WiepAxiSlvMp<C_OWNER>, &WiepAxiSlvMp<C_OWNER>::popWFunc,
                &WiepAxiSlvMp<C_OWNER>::retryWFunc> *m_wFifo;
    WiepTlmFifo<PacketPtr, WiepAxiSlvMp<C_OWNER>, &WiepAxiSlvMp<C_OWNER>::popArFunc,
                &WiepAxiSlvMp<C_OWNER>::retryArFunc> *m_arFifo;
    WiepTlmFifo<PacketPtr, WiepAxiSlvMp<C_OWNER>, &WiepAxiSlvMp<C_OWNER>::popBFunc,
                &WiepAxiSlvMp<C_OWNER>::pushBFunc> *m_bFifo;
    WiepTlmFifo<PacketPtr, WiepAxiSlvMp<C_OWNER>, &WiepAxiSlvMp<C_OWNER>::popRFunc,
                &WiepAxiSlvMp<C_OWNER>::pushRFunc> *m_rFifo;
    PacketPtr m_awRetryTrf;
    PacketPtr m_wRetryTrf;
    PacketPtr m_arRetryTrf;
    void setRChannelPrintCsvEn(bool en)
    {
        m_rChannelPrintCsvEn = en;
    }
    void setBChannelPrintCsvEn(bool en)
    {
        m_bChannelPrintCsvEn = en;
    }

  private:
    bool m_wrReqWait;
    bool m_wReqWait;
    bool m_rdReqWait;
    bool m_rChannelPrintCsvEn;
    bool m_bChannelPrintCsvEn;
    uint32_t m_wrOuts;
    uint32_t m_rdOuts;
    uint32_t m_wrOstCnt;
    uint32_t m_rdOstCnt;
    std::map<uint64_t, uint32_t> m_mapKTagVBeatcnt;

  public:
    uint32_t m_debugLevel;
    bool firstReqFlag;
    map<uint64_t, double> m_reqTimeMap;
    void timeCheck()
    {
        double curr = curTick();
        for (map<uint64_t, double>::iterator m_it = m_reqTimeMap.begin();
             m_it != m_reqTimeMap.end(); m_it++)
        {
            if (curr - m_it->second >= 1000 * 10000)
            {
                DBG_DUMP << "tag = 0x" << hex << m_it->first << ", recv tick = " << m_it->second
                         << ", curr tick = " << curr << ", gap = " << (curr - m_it->second) / 1000
                         << endl;
                WIEP_ASSERT(false);
            }
        }
        WiepTrigger<WiepAxiSlvMp, &WiepAxiSlvMp::timeCheck> trig(this, 1000 * 1000);
    }
    EventWrapper<WiepAxiSlvMp, &WiepAxiSlvMp::timeCheck> checkTimeOut;
    void recvTimingReqMp()
    {
        applyPendingReadyState();
        receiveMstReadyState();

        if (m_awMpChannel != NULL && !m_awFifo->full() && m_wrOstCnt < m_wrOuts)
        {
            WiepMp::WiepAxiAwMpPacket wire;
            if (m_awMpChannel->nbGet(wire) && wire.visibleTick <= curTick())
            {
                PacketPtr pkt = WiepMp::awMpToPacket(wire);
                pkt->wiepReq->setChanType(AW);
                if (recvTimingReq(pkt))
                    m_awMpChannel->delTrf();
            }
        }

        if (m_wMpChannel != NULL && !m_wFifo->full())
        {
            WiepMp::WiepAxiWMpPacket wire;
            if (m_wMpChannel->nbGet(wire) && wire.visibleTick <= curTick())
            {
                PacketPtr pkt = WiepMp::wMpToPacket(wire);
                pkt->wiepReq->setChanType(W);
                if (recvTimingReq(pkt))
                    m_wMpChannel->delTrf();
            }
        }

        if (m_arMpChannel != NULL && !m_arFifo->full() && m_rdOstCnt < m_rdOuts)
        {
            WiepMp::WiepAxiArMpPacket wire;
            if (m_arMpChannel->nbGet(wire) && wire.visibleTick <= curTick())
            {
                PacketPtr pkt = WiepMp::arMpToPacket(wire);
                pkt->wiepReq->setChanType(AR);
                if (recvTimingReq(pkt))
                    m_arMpChannel->delTrf();
            }
        }

        updateSlvReadyState();

        // Retry B/R packets retained in their local FIFO.
        popBFunc();
        popRFunc();
        schedule(m_recvTimingReqEvent, curTick() + m_obj->clockPeriod());
    }

    MpAwChannel *m_awMpChannel;
    MpWChannel *m_wMpChannel;
    MpBChannel *m_bMpChannel;
    MpArChannel *m_arMpChannel;
    MpRChannel *m_rMpChannel;
    MpReadyTxFifo *m_readyTxMpFifo;
    MpReadyRxFifo *m_readyRxMpFifo;
    EventWrapper<WiepAxiSlvMp, &WiepAxiSlvMp::recvTimingReqMp> m_recvTimingReqEvent;
    bool m_rReady;
    bool m_bReady;
    bool m_pendingRReady;
    bool m_pendingBReady;
    bool m_pendingReadyValid;
    Tick m_pendingReadyApplyTick;
    bool m_lastAwReady;
    bool m_lastWReady;
    bool m_lastArReady;

  public:
    WiepAxiSlvMp(const wiep_module_name &_name, MemObject *_owner, const AXITrfPack *_pack,
                 PortID _id = InvalidPortID, bool _bindWiep = true,
                 const WiepMp::WiepAxiMpConfig *_mpConfig = NULL)
        : WiepSlavePort(_name, _owner, _id, _bindWiep), m_obj(dynamic_cast<C_OWNER *>(_owner)),
          m_awRetryTrf(NULL), m_wRetryTrf(NULL), m_arRetryTrf(NULL), m_wrReqWait(false),
          m_wReqWait(false), m_rdReqWait(false), m_wrOuts(_pack->wrOuts), m_rdOuts(_pack->rdOuts),
          m_debugLevel(_pack->debugLevel), m_wrOstCnt(0), m_rdOstCnt(0), checkTimeOut(this),
          firstReqFlag(false), m_rChannelPrintCsvEn(false), m_bChannelPrintCsvEn(false),
          m_awMpChannel(NULL), m_wMpChannel(NULL), m_bMpChannel(NULL), m_arMpChannel(NULL),
          m_rMpChannel(NULL), m_readyTxMpFifo(NULL), m_readyRxMpFifo(NULL),
          m_recvTimingReqEvent(this), m_rReady(true), m_bReady(true), m_pendingRReady(true),
          m_pendingBReady(true), m_pendingReadyValid(false), m_pendingReadyApplyTick(0),
          m_lastAwReady(true), m_lastWReady(true), m_lastArReady(true)
    {
        para = new Para();
        para->m_debugLevel = m_debugLevel;
        m_awFifo =
            new WiepTlmFifo<PacketPtr, WiepAxiSlvMp<C_OWNER>, &WiepAxiSlvMp<C_OWNER>::popAwFunc,
                            &WiepAxiSlvMp<C_OWNER>::retryAwFunc>("m_awfifo", this, 16, 0,
                                                                 m_debugLevel);
        m_wFifo =
            new WiepTlmFifo<PacketPtr, WiepAxiSlvMp<C_OWNER>, &WiepAxiSlvMp<C_OWNER>::popWFunc,
                            &WiepAxiSlvMp<C_OWNER>::retryWFunc>("m_wfifo", this, 16, 1,
                                                                m_debugLevel);
        m_arFifo =
            new WiepTlmFifo<PacketPtr, WiepAxiSlvMp<C_OWNER>, &WiepAxiSlvMp<C_OWNER>::popArFunc,
                            &WiepAxiSlvMp<C_OWNER>::retryArFunc>("m_arfifo", this, 16, 0,
                                                                 m_debugLevel);
        m_bFifo =
            new WiepTlmFifo<PacketPtr, WiepAxiSlvMp<C_OWNER>, &WiepAxiSlvMp<C_OWNER>::popBFunc,
                            &WiepAxiSlvMp<C_OWNER>::pushBFunc>("m_bfifo", this, 16, 2,
                                                               m_debugLevel);
        m_rFifo =
            new WiepTlmFifo<PacketPtr, WiepAxiSlvMp<C_OWNER>, &WiepAxiSlvMp<C_OWNER>::popRFunc,
                            &WiepAxiSlvMp<C_OWNER>::pushRFunc>("m_rfifo", this, 16, 1,
                                                               m_debugLevel);
        m_awFifo->setPeriod(1000);
        m_wFifo->setPeriod(1000);
        m_arFifo->setPeriod(1000);
        m_rFifo->setPeriod(1000);
        m_bFifo->setPeriod(1000);
        m_awRetryTrf = getPoolPkt();
        m_awRetryTrf->wiepReq->setChanType(AW);
        m_arRetryTrf = getPoolPkt();
        m_arRetryTrf->wiepReq->setChanType(AR);
        m_wRetryTrf = getPoolPkt();
        m_wRetryTrf->wiepReq->setChanType(W);

        if (_mpConfig != NULL)
        {
            initializeMpChannels(*_mpConfig);
            schedule(m_recvTimingReqEvent, curTick() + m_obj->clockPeriod());
        }
    }
    virtual ~WiepAxiSlvMp()
    {
        delete m_awMpChannel;
        delete m_wMpChannel;
        delete m_bMpChannel;
        delete m_arMpChannel;
        delete m_rMpChannel;
        delete m_readyTxMpFifo;
        delete m_readyRxMpFifo;
    }

  private:
    virtual AddrRangeList getAddrRanges() const {}

  protected:
    bool sendTimingResp(PacketPtr pkt)
    {
        applyPendingReadyState();
        const Tick visibleTick = curTick() + m_obj->clockPeriod();

        if (pkt->wiepReq->getChanType() == B && m_bMpChannel != NULL)
        {
            if (!m_bReady)
                return false;
            WiepMp::WiepAxiBMpPacket wire = WiepMp::packetToBMp(pkt);
            wire.visibleTick = visibleTick;
            return m_bMpChannel->nbWrite(wire);
        }
        if (pkt->wiepReq->getChanType() == R && m_rMpChannel != NULL)
        {
            if (!m_rReady)
                return false;
            WiepMp::WiepAxiRMpPacket wire = WiepMp::packetToRMp(pkt);
            wire.visibleTick = visibleTick;
            return m_rMpChannel->nbWrite(wire);
        }
        return false;
    }
    virtual Tick recvAtomic(PacketPtr pkt)
    {
        return m_obj->recvAtomic(0, pkt);
    }
    virtual void recvFunctional(PacketPtr pkt)
    {
        return;
    }
    virtual bool recvTimingReq(PacketPtr pkt)
    {
        if (!firstReqFlag)
        {
            firstReqFlag = true;
            timeCheck();
        }
        if (!m_bindWiep)
        {
            if (pkt->isRead())
            {
                if (m_arFifo->full())
                {
                    DBG_VERB << "m_arFifo full." << endl;
                    m_rdReqWait = true;
                    return false;
                }
                else if (m_rdOstCnt == m_rdOuts)
                {
                    DBG_VERB << "outstanding full." << endl;
                    m_rdReqWait = true;
                    return false;
                }
                else
                {
                    DBG_TRAN << "m_arFifo Receive Request with " << pkt->wiepReq->summary() << endl;
                    m_arFifo->nbWrite(pkt);
                    m_rdOstCnt++;
                    m_reqTimeMap[pkt->wiepReq->getTag()] = curTick();
                    return true;
                }
            }
            else if (pkt->isWrite())
            {
                // to be filled in
            }
            else
            {
                WIEP_ASSERT(false);
            }
        }

        if (pkt->wiepReq->getChanType() == AW)
        {
            if ((m_awFifo->full()) || (m_wrOstCnt == m_wrOuts))
            {
                DBG_VERB << "m_awFifo FIFO depth = 0x" << hex << m_awFifo->size()
                         << ", the current outstanding is " << m_wrOstCnt << endl;
                m_wrReqWait = true;
                return false;
            }
            else
            {
                DBG_TRAN << "m_awFifo Receive Request with " << pkt->wiepReq->summary() << endl;
                m_awFifo->nbWrite(pkt);
                WIEP_ASSERT(pkt->wiepReq->getSize() != -1);
                m_wrOstCnt++;
                m_reqTimeMap[pkt->wiepReq->getTag()] = curTick();
                return true;
            }
        }
        else if (pkt->wiepReq->getChanType() == W)
        {
            if (m_wFifo->full())
            {
                m_wReqWait = true;
                DBG_VERB << "m_wFifo FIFO full." << endl;
                return false;
            }
            else
            {
                m_wFifo->nbWrite(pkt);
                WIEP_ASSERT(pkt->wiepReq->getSize() != -1);
                return true;
            }
        }
        else if ((pkt->wiepReq->getChanType() == AR))
        {
            if (m_arFifo->full() || (m_rdOstCnt == m_rdOuts))
            {
                DBG_VERB << "m_arFifo FIFO depth = 0x" << hex << m_arFifo->size()
                         << ", the current outstanding is " << m_rdOstCnt << endl;
                m_rdReqWait = true;
                return false;
            }
            else
            {
                DBG_TRAN << "m_arFifo Receive Request with " << pkt->wiepReq->summary() << endl;
                m_arFifo->nbWrite(pkt);
                WIEP_ASSERT(pkt->wiepReq->getSize() != -1);
                m_rdOstCnt++;
                m_mapKTagVBeatcnt[pkt->wiepReq->getTag()] = pkt->wiepReq->getBurstLength() + 1;
                m_reqTimeMap[pkt->wiepReq->getTag()] = curTick();
                return true;
            }
        }
        else
        {
            DBG_DUMP << "name = " << name() << ", ERROR with pkt " << pkt->wiepReq->summary()
                     << endl;
            WIEP_ASSERT(false);
            return false;
        }
    }
    virtual void recvRetry() override
    {
        if (m_retryChn == B)
        {
            DBG_VERB << "retry B transfer." << endl;
            popBFunc();
        }
        else if (m_retryChn == R)
        {
            DBG_VERB << "retry R transfer." << endl;
            popRFunc();
        }
        else
        {
            WIEP_ASSERT(false);
        }
    }

  private:
    void applyPendingReadyState()
    {
        if (!m_pendingReadyValid || curTick() < m_pendingReadyApplyTick)
        {
            return;
        }

        m_rReady = m_pendingRReady;
        m_bReady = m_pendingBReady;
        m_pendingReadyValid = false;
    }

    void receiveMstReadyState()
    {
        if (m_readyRxMpFifo == NULL)
            return;

        WiepMp::WiepAxiMstReadyMpPacket state = {};
        if (!m_readyRxMpFifo->nbRead(state))
            return;

        m_pendingRReady = state.rReady;
        m_pendingBReady = state.bReady;
        m_pendingReadyApplyTick = curTick() + m_obj->clockPeriod();
        m_pendingReadyValid = true;
    }

    void updateSlvReadyState()
    {
        if (m_readyTxMpFifo == NULL)
            return;

        const bool awReady = !m_awFifo->full();
        const bool wReady = !m_wFifo->full();
        const bool arReady = !m_arFifo->full();
        if (awReady == m_lastAwReady && wReady == m_lastWReady && arReady == m_lastArReady)
        {
            return;
        }

        WiepMp::WiepAxiSlvReadyMpPacket state = {};
        state.awReady = awReady;
        state.wReady = wReady;
        state.arReady = arReady;
        if (m_readyTxMpFifo->nbWrite(state))
        {
            m_lastAwReady = awReady;
            m_lastWReady = wReady;
            m_lastArReady = arReady;
        }
    }

    void initializeMpChannels(const WiepMp::WiepAxiMpConfig &config)
    {
        m_awMpChannel =
            new MpAwChannel(WiepMp::makeSharedChannelConfig(config, WiepMp::AxiChannel::AW));
        m_wMpChannel =
            new MpWChannel(WiepMp::makeSharedChannelConfig(config, WiepMp::AxiChannel::W));
        m_bMpChannel =
            new MpBChannel(WiepMp::makeSharedChannelConfig(config, WiepMp::AxiChannel::B));
        m_arMpChannel =
            new MpArChannel(WiepMp::makeSharedChannelConfig(config, WiepMp::AxiChannel::AR));
        m_rMpChannel =
            new MpRChannel(WiepMp::makeSharedChannelConfig(config, WiepMp::AxiChannel::R));
        m_readyTxMpFifo = new MpReadyTxFifo(
            WiepMp::makeSharedChannelConfig(config, WiepMp::AxiChannel::SlvReady));
        m_readyRxMpFifo = new MpReadyRxFifo(
            WiepMp::makeSharedChannelConfig(config, WiepMp::AxiChannel::MstReady));

        const bool initialized = m_awMpChannel->initialize() && m_wMpChannel->initialize() &&
                                 m_bMpChannel->initialize() && m_arMpChannel->initialize() &&
                                 m_rMpChannel->initialize() && m_readyTxMpFifo->initialize() &&
                                 m_readyRxMpFifo->initialize();
        WIEP_ASSERT(initialized);
    }
};
} // namespace wiep_tlm_common
#endif /* //WIEP_AXI_SLV_MP_HH */
