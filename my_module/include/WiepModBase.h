//
// Created by ZevenKK on 2026/8/13.
//

#ifndef GEM5_CLION_WORKSPACE_WIEPMODBASE_H
#define GEM5_CLION_WORKSPACE_WIEPMODBASE_H

#include "WiepPlatformRegistry.h"

#include <list>
#include <string>
class wiep_module_name
{
  public:
    wiep_module_name(const std::string &_name) : m_nameStr("")
    {
        g_add_hier(_name);
        std::list<std::string>::iterator lend = g_hierStack.end();
        lend--;
        for (auto it = g_hierStack.begin(); it != g_hierStack.end(); ++it)
        {
            if (it == lend)
            {
                m_nameStr += *it;
            }
            else
            {
                m_nameStr += *it;
                m_nameStr += ".";
            }
        }
    }
    wiep_module_name(const char *_name) : m_nameStr("")
    {
        g_add_hier(std::string(_name));
        std::list<std::string>::iterator lend = g_hierStack.end();
        lend--;
        for (std::list<std::string>::iterator it = g_hierStack.begin(); it != g_hierStack.end();
             ++it)
        {
            if (it == lend)
            {
                m_nameStr += *it;
            }
            else
            {
                m_nameStr += *it;
                m_nameStr += ".";
            }
        }
    }
    ~wiep_module_name()
    {
        g_pop_hier();
    }
    operator const char *() const
    {
        return m_nameStr.c_str();
    }
    std::string to_str() const
    {
        return m_nameStr;
    }

  private:
    static inline void g_add_hier(const std::string &_str)
    {
        g_hierStack.push_back(_str);
    }
    static inline void g_pop_hier()
    {
        if (g_hierStack.size())
            g_hierStack.pop_back();
    }
    static std::list<std::string> g_hierStack;
    std::string m_nameStr;
};
class WiepModBase : public MemObject
{
  public:
    WiepModBase(const wiep_module_name &_name, const WiepModBaseParams *_params)
        : MemObject(_params), m_name(_name), atomicFlag(false)
    {
        m_platformNodeId = WiepPlatformRegistry::instance().registerNode(this, m_name);
        _params->setPeriod();
        _params->checkPara();
    }
    ~WiepModBase() {}
    const std::string name() const
    {
        return m_name;
    }
    const std::string &platformNodeId() const
    {
        return m_platformNodeId;
    }
    void setPlatformCppType(const std::string &cppType)
    {
        WiepPlatformRegistry::instance().setNodeCppType(this, cppType);
    }
    bool dumpPlatformYaml(const std::string &path) const
    {
        return WiepPlatformRegistry::instance().dumpYaml(path, m_platformNodeId);
    }
    void SchPosEvent(Event *_event, Tick _clock)
    {
        schedule(_event, curTick());
    }
    void SchNegEvent(Event *_event, Tick _clock)
    {
        schedule(_event, curTick() + _clock / 2);
    }
    virtual Tick recvAtomicPkt(PacketPtr pkt) {}
    virtual void RecordCSV(wiep_tlm_common::chanTypeEnum channel, PacketPtr pkt) {}

  protected:
    std::string m_name;
    std::string m_platformNodeId;

  private:
    bool atomicFlag;
};

class WiepCluster : public WiepModBase
{
  public:
    WiepCluster(const wiep_module_name &_name, const WiepModBaseParams *_params)
        : WiepModBase(_name, _params)
    {
        WiepPlatformRegistry::instance().markCluster(this);
    }
};

#endif // GEM5_CLION_WORKSPACE_WIEPMODBASE_H
