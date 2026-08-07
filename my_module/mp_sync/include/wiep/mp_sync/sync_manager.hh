#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "wiep/mp_sync/common.hh"
#include "wiep/mp_sync/posix_ipc.hh"

namespace wiep::mp
{

class MpSyncManager
{
  public:
    MpSyncManager(const std::string &session, MpRole role,
                  std::uint32_t worker_count,
                  std::uint32_t worker_id = 0);
    ~MpSyncManager();

    MpSyncManager(const MpSyncManager &) = delete;
    MpSyncManager &operator=(const MpSyncManager &) = delete;

    void registerChannel(IMpSyncChannel &channel);

    void beginInterval(std::uint64_t sync_id);
    void endInterval(std::uint64_t sync_id);

    bool waitInterval(std::uint64_t &sync_id);
    void finishInterval(std::uint64_t sync_id);

    void stop();

  private:
    struct WorkerState
    {
        std::uint64_t sync_id;
        std::uint32_t command;
        std::uint32_t status;
        std::uint32_t error_code;
        std::uint32_t reserved;
    };

    struct ControlBlock
    {
        std::uint32_t magic;
        std::uint32_t version;
        std::uint32_t worker_count;
        std::uint32_t reserved;
        WorkerState workers[MaxWorkers];
    };

    void initializeControl();
    void validateControl() const;
    void importChannels(std::uint64_t sync_id);
    void exportChannels(std::uint64_t sync_id);
    void checkRole(MpRole expected, const char *operation) const;

    std::string session_;
    MpRole role_;
    std::uint32_t workerCount_;
    std::uint32_t workerId_;
    SharedMemoryRegion controlRegion_;
    ControlBlock *control_;
    std::vector<IMpSyncChannel *> channels_;
    std::vector<std::unique_ptr<NamedSemaphore>> startSemaphores_;
    std::vector<std::unique_ptr<NamedSemaphore>> doneSemaphores_;
    bool stopped_ = false;
};

} // namespace wiep::mp
