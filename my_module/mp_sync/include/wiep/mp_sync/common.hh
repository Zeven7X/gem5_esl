#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace wiep
{
namespace mp
{

constexpr std::uint32_t ProtocolVersion = 1;
constexpr std::uint32_t ChannelMagic = 0x57465043;
constexpr std::uint32_t ControlMagic = 0x57465053;
constexpr std::size_t MaxWorkers = 64;

enum class MpRole : std::uint32_t
{
    Main = 0,
    Worker = 1,
};

enum class MpDirection : std::uint32_t
{
    MainToWorker = 0,
    WorkerToMain = 1,
};

enum class MpCommand : std::uint32_t
{
    None = 0,
    Run = 1,
    Exit = 2,
};

enum class MpStatus : std::uint32_t
{
    Idle = 0,
    Running = 1,
    Done = 2,
    Error = 3,
};

class MpError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

inline std::uint64_t
stableHash(const std::string &text)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char ch : text) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    return hash;
}

class IMpSyncChannel
{
  public:
    virtual ~IMpSyncChannel() = default;

    virtual void beginInterval(std::uint64_t sync_id) = 0;
    virtual void endInterval(std::uint64_t sync_id) = 0;
    virtual const std::string &name() const = 0;
    virtual std::uint32_t workerId() const = 0;
};

} // namespace mp
} // namespace wiep
