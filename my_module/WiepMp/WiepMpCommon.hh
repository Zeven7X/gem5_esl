#ifndef WIEP_MP_COMMON_HH
#define WIEP_MP_COMMON_HH

#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace WiepMp
{

static const std::uint64_t ControlMagic = 0x574945504d504354ULL;
static const std::uint64_t ChannelMagic = 0x574945504d504348ULL;
static const std::uint32_t ProtocolVersion = 1;
static const std::uint32_t MaxProcessCount = 64;

enum class ProcessState : std::uint32_t
{
    Empty = 0,
    Registering,
    Initializing,
    Ready,
    Running,
    AtBarrier,
    Finished,
    Failed
};

enum class GlobalState : std::uint32_t
{
    Empty = 0,
    Initializing,
    WaitingProcesses,
    Running,
    Finished,
    Failed
};

class WiepMpError : public std::runtime_error
{
  public:
    explicit WiepMpError(const std::string &message)
        : std::runtime_error(message)
    {
    }
};

struct ProcessManagerConfig
{
    std::string sessionName;
    std::string processName;
    std::uint32_t processId;
    std::uint32_t processCount;
    std::uint64_t syncPeriodTicks;
    std::uint32_t startupTimeoutSeconds;
    std::uint32_t barrierTimeoutSeconds;
    bool unlinkOnExit;
    bool debug;

    ProcessManagerConfig()
        : processId(0), processCount(1), syncPeriodTicks(1),
          startupTimeoutSeconds(60), barrierTimeoutSeconds(60),
          unlinkOnExit(true), debug(false)
    {
    }
};

inline std::string
sanitizeName(const std::string &name)
{
    if (name.empty())
        throw WiepMpError("IPC name must not be empty");

    std::string result;
    result.reserve(name.size());
    for (std::string::const_iterator it = name.begin(); it != name.end(); ++it) {
        const unsigned char ch = static_cast<unsigned char>(*it);
        result.push_back(std::isalnum(ch) || ch == '_' || ch == '-'
                             ? static_cast<char>(ch)
                             : '_');
    }
    return result;
}

inline std::string
controlShmName(const std::string &session)
{
    return "/wiep_mp_ctrl_" + sanitizeName(session);
}

inline std::string
channelShmName(const std::string &session, std::uint32_t channel_id)
{
    return "/wiep_mp_channel_" + sanitizeName(session) + "_" +
           std::to_string(channel_id);
}

} // namespace WiepMp

#endif // WIEP_MP_COMMON_HH
