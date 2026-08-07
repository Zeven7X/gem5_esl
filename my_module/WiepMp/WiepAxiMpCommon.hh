#ifndef WIEP_AXI_MP_COMMON_HH
#define WIEP_AXI_MP_COMMON_HH

#include <algorithm>
#include <cstdint>
#include <string>
#include <type_traits>

#include "WiepSharedChannel.hh"

namespace WiepMp
{

enum class AxiChannel : std::uint32_t
{
    AW = 0,
    W = 1,
    B = 2,
    AR = 3,
    R = 4
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

template <typename WirePacket>
struct AxiMpEnvelope
{
    std::uint64_t visibleEpoch;
    WirePacket packet;
};

inline std::uint32_t
axiChannelId(std::uint32_t interfaceId, AxiChannel channel)
{
    // Eight IDs are reserved per interface; five are currently used by AXI.
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

// PacketPtr stays local. nbWrite encodes it directly into shared memory, while
// nbGet/nbRead reconstruct a process-local PacketPtr through Codec.
//
// Required Codec interface:
//   bool encode(PacketPtr, AxiChannel, WirePacket &);
//   PacketPtr decode(const WirePacket &, AxiChannel);
template <typename WirePacket, typename Codec, std::uint32_t Capacity = 256>
class WiepAxiSharedFifo
{
    static_assert(std::is_trivially_copyable<WirePacket>::value,
                  "WirePacket must be trivially copyable");
    static_assert(!std::is_pointer<WirePacket>::value,
                  "WirePacket cannot be a process-local pointer");

    typedef AxiMpEnvelope<WirePacket> Envelope;
    typedef WiepSharedChannel<Envelope, Capacity> Channel;

  public:
    WiepAxiSharedFifo(const WiepAxiMpConfig &config, AxiChannel axiChannel,
                      Codec &codec)
        : axiChannel_(axiChannel), codec_(codec),
          channel_(makeSharedChannelConfig(config, axiChannel)),
          currentEpoch_(0), cachedPacket_(NULL), cachedPacketValid_(false),
          writeBlocked_(false)
    {
    }

    bool initialize() { return channel_.initialize(); }

    bool nbWrite(PacketPtr packet)
    {
        Envelope envelope = Envelope();
        if (!codec_.encode(packet, axiChannel_, envelope.packet))
            return false;

        // A command generated in epoch N is readable after barrier N -> N+1.
        envelope.visibleEpoch = currentEpoch_ + 1U;
        if (channel_.nbWrite(envelope))
            return true;

        writeBlocked_ = true;
        return false;
    }

    PacketPtr nbRead()
    {
        if (!ensureCached())
            return NULL;

        PacketPtr packet = cachedPacket_;
        delTrf();
        return packet;
    }

    bool nbRead(PacketPtr &packet)
    {
        if (!ensureCached())
            return false;

        packet = cachedPacket_;
        return delTrf();
    }

    PacketPtr &nbGet()
    {
        if (!ensureCached())
            return nullPacket();
        return cachedPacket_;
    }

    bool nbGet(PacketPtr &packet)
    {
        if (!ensureCached())
            return false;
        packet = cachedPacket_;
        return true;
    }

    bool delTrf()
    {
        if (!ensureCached() || !channel_.delTrf())
            return false;

        cachedPacket_ = NULL;
        cachedPacketValid_ = false;
        return true;
    }

    bool canPop() { return ensureCached(); }
    bool checkTrf() { return canPop(); }
    bool empty() { return !canPop(); }
    bool full() const { return channel_.full(); }

    // rxSize may include future-epoch entries. Use canPop() for readability.
    std::uint32_t size() const { return channel_.rxSize(); }
    std::uint32_t emptyNum() const { return channel_.txFreeSize(); }
    std::uint32_t capacity() const { return channel_.usableCapacity(); }

    void setCurrentEpoch(std::uint64_t epoch)
    {
        currentEpoch_ = epoch;
    }

    bool consumeWriteRetry()
    {
        if (!writeBlocked_ || channel_.full())
            return false;
        writeBlocked_ = false;
        return true;
    }

    AxiChannel axiChannel() const { return axiChannel_; }
    std::uint32_t channelId() const { return channel_.channelId(); }

  private:
    bool ensureCached()
    {
        if (cachedPacketValid_)
            return true;

        Envelope envelope;
        if (!channel_.nbGet(envelope) ||
            envelope.visibleEpoch > currentEpoch_) {
            return false;
        }

        cachedPacket_ = codec_.decode(envelope.packet, axiChannel_);
        cachedPacketValid_ = cachedPacket_ != NULL;
        return cachedPacketValid_;
    }

    static PacketPtr &nullPacket()
    {
        static PacketPtr packet = NULL;
        return packet;
    }

    const AxiChannel axiChannel_;
    Codec &codec_;
    Channel channel_;
    std::uint64_t currentEpoch_;
    PacketPtr cachedPacket_;
    bool cachedPacketValid_;
    bool writeBlocked_;
};

} // namespace WiepMp

#endif // WIEP_AXI_MP_COMMON_HH
