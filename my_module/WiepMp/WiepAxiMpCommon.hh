#ifndef WIEP_AXI_MP_COMMON_HH
#define WIEP_AXI_MP_COMMON_HH

#include <algorithm>
#include <cstdint>
#include <string>

#include "WiepSharedChannel.hh"

namespace WiepMp
{

enum class AxiChannel : std::uint32_t
{
    AW = 0,
    W = 1,
    B = 2,
    AR = 3,
    R = 4,
    MstReady = 5,
    SlvReady = 6
};

struct WiepAxiMpConfig
{
    std::string sessionName;
    std::string interfaceName;
    std::uint32_t interfaceId;
    std::uint32_t localProcessId;
    std::uint32_t remoteProcessId;
    std::uint32_t attachTimeoutSeconds;
    bool unlinkOnExit;
    bool debug;

    WiepAxiMpConfig()
        : interfaceId(0), localProcessId(0), remoteProcessId(1),
          attachTimeoutSeconds(60), unlinkOnExit(true), debug(false)
    {
    }
};

struct WiepAxiAwMpPacket
{
    std::uint64_t visibleTick;
    std::uint64_t tag;
    std::uint64_t address;
    std::uint32_t burstLength;
    std::uint32_t size;
    std::uint8_t qos;
    std::uint8_t reserved[7];
};

struct WiepAxiWMpPacket
{
    static const std::uint32_t MaxDataBytes = 64;

    std::uint64_t visibleTick;
    std::uint64_t tag;
    std::uint64_t strobe;
    std::uint32_t dataLength;
    std::uint8_t last;
    std::uint8_t reserved[3];
    std::uint8_t data[MaxDataBytes];
};

struct WiepAxiBMpPacket
{
    std::uint64_t visibleTick;
    std::uint64_t tag;
    std::uint8_t response;
    std::uint8_t qos;
    std::uint8_t reserved[6];
};

struct WiepAxiArMpPacket
{
    std::uint64_t visibleTick;
    std::uint64_t tag;
    std::uint64_t address;
    std::uint32_t burstLength;
    std::uint32_t size;
    std::uint8_t qos;
    std::uint8_t reserved[7];
};

struct WiepAxiRMpPacket
{
    static const std::uint32_t MaxDataBytes = 64;

    std::uint64_t visibleTick;
    std::uint64_t tag;
    std::uint32_t dataLength;
    std::uint8_t response;
    std::uint8_t last;
    std::uint8_t reserved[2];
    std::uint8_t data[MaxDataBytes];
};

struct WiepAxiSlvReadyMpPacket
{
    bool awReady;
    bool wReady;
    bool arReady;
};

struct WiepAxiMstReadyMpPacket
{
    bool rReady;
    bool bReady;
};

// These are intentionally incomplete conversion hooks. Fill the AXI fields
// from pkt->wiepReq when the final wire format is agreed.
inline WiepAxiAwMpPacket
packetToAwMp(PacketPtr pkt)
{
    (void)pkt;
    return WiepAxiAwMpPacket();
}

inline WiepAxiWMpPacket
packetToWMp(PacketPtr pkt)
{
    (void)pkt;
    return WiepAxiWMpPacket();
}

inline WiepAxiBMpPacket
packetToBMp(PacketPtr pkt)
{
    (void)pkt;
    return WiepAxiBMpPacket();
}

inline WiepAxiArMpPacket
packetToArMp(PacketPtr pkt)
{
    (void)pkt;
    return WiepAxiArMpPacket();
}

inline WiepAxiRMpPacket
packetToRMp(PacketPtr pkt)
{
    (void)pkt;
    return WiepAxiRMpPacket();
}

// Reverse hooks currently allocate only a local Packet. The caller sets the
// AXI channel; all remaining wiepReq and payload fields are TODO.
inline PacketPtr
awMpToPacket(const WiepAxiAwMpPacket &wire)
{
    (void)wire;
    return getPoolPkt();
}

inline PacketPtr
wMpToPacket(const WiepAxiWMpPacket &wire)
{
    (void)wire;
    return getPoolPkt();
}

inline PacketPtr
bMpToPacket(const WiepAxiBMpPacket &wire)
{
    (void)wire;
    return getPoolPkt();
}

inline PacketPtr
arMpToPacket(const WiepAxiArMpPacket &wire)
{
    (void)wire;
    return getPoolPkt();
}

inline PacketPtr
rMpToPacket(const WiepAxiRMpPacket &wire)
{
    (void)wire;
    return getPoolPkt();
}

inline std::uint32_t
axiChannelId(std::uint32_t interfaceId, AxiChannel channel)
{
    return interfaceId * 8U + static_cast<std::uint32_t>(channel);
}

inline SharedChannelConfig
makeSharedChannelConfig(const WiepAxiMpConfig &config, AxiChannel channel)
{
    SharedChannelConfig channelConfig;
    channelConfig.sessionName = config.sessionName;
    channelConfig.channelId = axiChannelId(config.interfaceId, channel);
    channelConfig.processId = config.localProcessId;
    channelConfig.processA = std::min(config.localProcessId,
                                      config.remoteProcessId);
    channelConfig.processB = std::max(config.localProcessId,
                                      config.remoteProcessId);
    channelConfig.attachTimeoutSeconds = config.attachTimeoutSeconds;
    channelConfig.unlinkOnExit = config.unlinkOnExit;
    channelConfig.debug = config.debug;
    return channelConfig;
}

// WiepSharedChannel reserves one ring slot, so Depth + 1 gives the requested
// usable channel depth.
template <typename T, std::uint32_t Depth = 16>
using WiepAxiMpChannel = WiepSharedChannel<T, Depth + 1U>;

} // namespace WiepMp

#endif // WIEP_AXI_MP_COMMON_HH
