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

// Codec is supplied by the model because PacketPtr is process-local.
// Required interface:
//   bool encode(PacketPtr, AxiChannel, WirePacket &);
//   PacketPtr decode(const WirePacket &, AxiChannel);
template <typename WirePacket, typename Codec, std::uint32_t Capacity = 256>
class WiepAxiMpTransport
{
    static_assert(std::is_trivially_copyable<WirePacket>::value,
                  "WirePacket must be trivially copyable");
    static_assert(!std::is_pointer<WirePacket>::value,
                  "WirePacket cannot be a process-local pointer");

  protected:
    typedef AxiMpEnvelope<WirePacket> Envelope;
    typedef WiepSharedChannel<Envelope, Capacity> Channel;

    WiepAxiMpTransport(const WiepAxiMpConfig &config, Codec &codec)
        : config_(config), codec_(codec), currentEpoch_(0),
          awChannel_(makeSharedChannelConfig(config, AxiChannel::AW)),
          wChannel_(makeSharedChannelConfig(config, AxiChannel::W)),
          bChannel_(makeSharedChannelConfig(config, AxiChannel::B)),
          arChannel_(makeSharedChannelConfig(config, AxiChannel::AR)),
          rChannel_(makeSharedChannelConfig(config, AxiChannel::R))
    {
    }

    bool initializeTransport()
    {
        return awChannel_.initialize() && wChannel_.initialize() &&
               bChannel_.initialize() && arChannel_.initialize() &&
               rChannel_.initialize();
    }

    void setCurrentEpoch(std::uint64_t epoch) { currentEpoch_ = epoch; }

    bool sendPacket(PacketPtr packet, AxiChannel axiChannel, Channel &channel)
    {
        Envelope envelope = Envelope();
        if (!codec_.encode(packet, axiChannel, envelope.packet))
            return false;

        // Data produced in epoch N becomes visible after barrier N -> N+1.
        envelope.visibleEpoch = currentEpoch_ + 1U;
        return channel.nbWrite(envelope);
    }

    template <typename Fifo>
    bool receivePacket(AxiChannel axiChannel, Channel &channel, Fifo *fifo)
    {
        Envelope envelope;
        if (fifo->full() || !channel.nbGet(envelope) ||
            envelope.visibleEpoch > currentEpoch_) {
            return false;
        }

        PacketPtr packet = codec_.decode(envelope.packet, axiChannel);
        if (packet == NULL || !fifo->nbWrite(packet))
            return false;

        return channel.delTrf();
    }

    const WiepAxiMpConfig config_;
    Codec &codec_;
    std::uint64_t currentEpoch_;
    Channel awChannel_;
    Channel wChannel_;
    Channel bChannel_;
    Channel arChannel_;
    Channel rChannel_;
};

} // namespace WiepMp

#endif // WIEP_AXI_MP_COMMON_HH
