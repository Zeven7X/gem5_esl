#ifndef WIEP_PROCESS_MANAGER_HH
#define WIEP_PROCESS_MANAGER_HH

#include <cstdint>
#include <pthread.h>
#include <string>

#include "WiepMpCommon.hh"

namespace WiepMp
{

class WiepProcessManager
{
  public:
    explicit WiepProcessManager(const ProcessManagerConfig &config);
    ~WiepProcessManager();

    WiepProcessManager(const WiepProcessManager &) = delete;
    WiepProcessManager &operator=(const WiepProcessManager &) = delete;

    bool initialize();
    bool markReady();
    bool waitForSimulationStart();
    bool synchronize(std::uint64_t epoch, std::uint64_t current_tick);
    void markFinished();
    void markFailed(std::int32_t error_code, const std::string &reason);
    void updateHeartbeat(std::uint64_t current_tick);

    bool isSocProcess() const { return config_.processId == 0; }
    std::uint32_t processId() const { return config_.processId; }
    std::uint32_t processCount() const { return config_.processCount; }
    std::uint64_t syncPeriodTicks() const { return config_.syncPeriodTicks; }
    bool terminateRequested() const;
    bool valid() const { return initialized_; }

    void dumpProcessStatus() const;

  private:
    struct SharedProcessStatus
    {
        std::uint32_t state;
        std::int32_t pid;
        std::int32_t errorCode;
        std::uint32_t reserved;
        std::uint64_t currentTick;
        std::uint64_t localEpoch;
        std::uint64_t heartbeat;
        char name[64];
    };

    struct SharedControlBlock
    {
        std::uint64_t magic;
        std::uint32_t version;
        std::uint32_t initialized;
        std::uint32_t expectedProcessCount;
        std::uint32_t globalState;
        std::uint32_t registeredCount;
        std::uint32_t readyCount;
        std::uint64_t syncPeriodTicks;
        std::uint64_t completedEpoch;
        std::int32_t ownerPid;
        std::uint32_t terminateRequested;
        char failureReason[128];
        pthread_mutex_t mutex;
        pthread_cond_t cond;
        SharedProcessStatus processes[MaxProcessCount];
    };

    bool createControlBlock();
    bool attachControlBlock();
    bool registerProcess();
    bool lockControl() const;
    void unlockControl() const;
    bool waitWithTimeout(const struct timespec &deadline) const;
    bool allProcessesReady() const;
    bool allProcessesAtBarrier(std::uint64_t epoch) const;
    bool allProcessesFinished() const;
    bool checkRunningState() const;
    void failLocked(std::int32_t error_code, const std::string &reason);
    void log(const std::string &message) const;
    struct timespec deadlineAfter(std::uint32_t seconds) const;

    ProcessManagerConfig config_;
    std::string shmName_;
    int shmFd_;
    SharedControlBlock *control_;
    bool owner_;
    bool initialized_;
    bool ready_;
    bool finished_;
    std::uint64_t nextEpoch_;
};

} // namespace WiepMp

#endif // WIEP_PROCESS_MANAGER_HH
