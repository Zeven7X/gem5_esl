#include "WiepProcessManager.hh"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace WiepMp
{

namespace
{

const char *
processStateName(ProcessState state)
{
    switch (state) {
      case ProcessState::Empty: return "Empty";
      case ProcessState::Registering: return "Registering";
      case ProcessState::Initializing: return "Initializing";
      case ProcessState::Ready: return "Ready";
      case ProcessState::Running: return "Running";
      case ProcessState::AtBarrier: return "AtBarrier";
      case ProcessState::Finished: return "Finished";
      case ProcessState::Failed: return "Failed";
    }
    return "Unknown";
}

} // namespace

WiepProcessManager::WiepProcessManager(const ProcessManagerConfig &config)
    : config_(config), shmName_(controlShmName(config.sessionName)),
      shmFd_(-1), control_(NULL), owner_(false), initialized_(false),
      ready_(false), finished_(false), nextEpoch_(0)
{
}

WiepProcessManager::~WiepProcessManager()
{
    if (control_ != NULL)
        munmap(control_, sizeof(SharedControlBlock));
    if (shmFd_ >= 0)
        close(shmFd_);
    if (owner_ && config_.unlinkOnExit)
        shm_unlink(shmName_.c_str());
}

bool
WiepProcessManager::initialize()
{
    if (initialized_)
        return true;
    if (config_.processCount == 0 ||
        config_.processCount > MaxProcessCount ||
        config_.processId >= config_.processCount ||
        config_.syncPeriodTicks == 0 || config_.sessionName.empty()) {
        std::cerr << "[WiepMp] invalid ProcessManagerConfig\n";
        return false;
    }

    const bool mapped = isSocProcess() ? createControlBlock()
                                       : attachControlBlock();
    if (!mapped || !registerProcess())
        return false;

    initialized_ = true;
    log("initialized");
    return true;
}

bool
WiepProcessManager::createControlBlock()
{
    shmFd_ = shm_open(shmName_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (shmFd_ < 0) {
        std::cerr << "[WiepMp][P0] cannot create " << shmName_ << ": "
                  << std::strerror(errno) << "\n";
        return false;
    }
    owner_ = true;

    if (ftruncate(shmFd_, sizeof(SharedControlBlock)) != 0) {
        std::cerr << "[WiepMp][P0] ftruncate failed: "
                  << std::strerror(errno) << "\n";
        return false;
    }

    void *mapping = mmap(NULL, sizeof(SharedControlBlock),
                         PROT_READ | PROT_WRITE, MAP_SHARED, shmFd_, 0);
    if (mapping == MAP_FAILED) {
        std::cerr << "[WiepMp][P0] mmap failed: " << std::strerror(errno)
                  << "\n";
        return false;
    }
    control_ = static_cast<SharedControlBlock *>(mapping);
    std::memset(control_, 0, sizeof(*control_));

    pthread_mutexattr_t mutexAttr;
    pthread_condattr_t condAttr;
    bool mutexAttrReady = pthread_mutexattr_init(&mutexAttr) == 0;
    bool condAttrReady = false;
    bool pthreadObjectsReady = mutexAttrReady &&
        pthread_mutexattr_setpshared(&mutexAttr, PTHREAD_PROCESS_SHARED) == 0;
#if defined(__linux__)
    if (pthreadObjectsReady) {
        pthreadObjectsReady =
            pthread_mutexattr_setrobust(&mutexAttr, PTHREAD_MUTEX_ROBUST) == 0;
    }
#endif
    if (pthreadObjectsReady) {
        condAttrReady = pthread_condattr_init(&condAttr) == 0;
        pthreadObjectsReady = condAttrReady &&
            pthread_condattr_setpshared(&condAttr, PTHREAD_PROCESS_SHARED) == 0 &&
            pthread_mutex_init(&control_->mutex, &mutexAttr) == 0 &&
            pthread_cond_init(&control_->cond, &condAttr) == 0;
    }
    if (!pthreadObjectsReady) {
        std::cerr << "[WiepMp][P0] cannot initialize process-shared pthread objects\n";
        if (mutexAttrReady)
            pthread_mutexattr_destroy(&mutexAttr);
        if (condAttrReady)
            pthread_condattr_destroy(&condAttr);
        return false;
    }
    pthread_mutexattr_destroy(&mutexAttr);
    pthread_condattr_destroy(&condAttr);

    control_->magic = ControlMagic;
    control_->version = ProtocolVersion;
    control_->expectedProcessCount = config_.processCount;
    control_->syncPeriodTicks = config_.syncPeriodTicks;
    control_->globalState = static_cast<std::uint32_t>(
        GlobalState::WaitingProcesses);
    control_->ownerPid = static_cast<std::int32_t>(getpid());
    __atomic_store_n(&control_->initialized, 1U, __ATOMIC_RELEASE);
    return true;
}

bool
WiepProcessManager::attachControlBlock()
{
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(config_.startupTimeoutSeconds);

    while (std::chrono::steady_clock::now() < deadline) {
        shmFd_ = shm_open(shmName_.c_str(), O_RDWR, 0600);
        if (shmFd_ >= 0)
            break;
        if (errno != ENOENT) {
            std::cerr << "[WiepMp][P" << config_.processId
                      << "] shm_open failed: " << std::strerror(errno) << "\n";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (shmFd_ < 0) {
        std::cerr << "[WiepMp][P" << config_.processId
                  << "] timed out waiting for " << shmName_ << "\n";
        return false;
    }

    void *mapping = mmap(NULL, sizeof(SharedControlBlock),
                         PROT_READ | PROT_WRITE, MAP_SHARED, shmFd_, 0);
    if (mapping == MAP_FAILED) {
        std::cerr << "[WiepMp][P" << config_.processId
                  << "] mmap failed: " << std::strerror(errno) << "\n";
        return false;
    }
    control_ = static_cast<SharedControlBlock *>(mapping);

    while (std::chrono::steady_clock::now() < deadline &&
           __atomic_load_n(&control_->initialized, __ATOMIC_ACQUIRE) == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (__atomic_load_n(&control_->initialized, __ATOMIC_ACQUIRE) == 0 ||
        control_->magic != ControlMagic ||
        control_->version != ProtocolVersion ||
        control_->expectedProcessCount != config_.processCount ||
        control_->syncPeriodTicks != config_.syncPeriodTicks) {
        std::cerr << "[WiepMp][P" << config_.processId
                  << "] control metadata mismatch\n";
        return false;
    }
    return true;
}

bool
WiepProcessManager::registerProcess()
{
    if (!lockControl())
        return false;

    SharedProcessStatus &status = control_->processes[config_.processId];
    if (status.state != static_cast<std::uint32_t>(ProcessState::Empty)) {
        unlockControl();
        std::cerr << "[WiepMp][P" << config_.processId
                  << "] process-id is already registered\n";
        return false;
    }

    status.state = static_cast<std::uint32_t>(ProcessState::Registering);
    status.pid = static_cast<std::int32_t>(getpid());
    status.errorCode = 0;
    status.currentTick = 0;
    status.localEpoch = 0;
    status.heartbeat = 1;
    const std::string processName = config_.processName.empty()
        ? "process_" + std::to_string(config_.processId)
        : config_.processName;
    std::snprintf(status.name, sizeof(status.name), "%s", processName.c_str());
    status.state = static_cast<std::uint32_t>(ProcessState::Initializing);
    ++control_->registeredCount;
    pthread_cond_broadcast(&control_->cond);
    unlockControl();
    return true;
}

bool
WiepProcessManager::markReady()
{
    if (!initialized_ || ready_ || !lockControl())
        return false;
    SharedProcessStatus &status = control_->processes[config_.processId];
    if (status.state != static_cast<std::uint32_t>(ProcessState::Initializing)) {
        unlockControl();
        return false;
    }
    status.state = static_cast<std::uint32_t>(ProcessState::Ready);
    ++status.heartbeat;
    ++control_->readyCount;
    ready_ = true;
    pthread_cond_broadcast(&control_->cond);
    unlockControl();
    log("Ready");
    return true;
}

bool
WiepProcessManager::waitForSimulationStart()
{
    if (!ready_ || !lockControl())
        return false;
    const struct timespec deadline = deadlineAfter(config_.startupTimeoutSeconds);

    if (isSocProcess()) {
        while (!allProcessesReady() &&
               control_->globalState != static_cast<std::uint32_t>(GlobalState::Failed)) {
            if (!waitWithTimeout(deadline)) {
                failLocked(1001, "startup timeout waiting for Ready processes");
                unlockControl();
                return false;
            }
        }
        if (control_->globalState == static_cast<std::uint32_t>(GlobalState::Failed)) {
            unlockControl();
            return false;
        }
        control_->globalState = static_cast<std::uint32_t>(GlobalState::Running);
        for (std::uint32_t id = 0; id < config_.processCount; ++id)
            control_->processes[id].state = static_cast<std::uint32_t>(ProcessState::Running);
        pthread_cond_broadcast(&control_->cond);
        log("start simulation");
    } else {
        while (control_->globalState != static_cast<std::uint32_t>(GlobalState::Running) &&
               control_->globalState != static_cast<std::uint32_t>(GlobalState::Failed)) {
            if (!waitWithTimeout(deadline)) {
                failLocked(1002, "startup timeout waiting for Coordinator");
                unlockControl();
                return false;
            }
        }
    }

    const bool running = checkRunningState();
    unlockControl();
    return running;
}

bool
WiepProcessManager::synchronize(std::uint64_t epoch,
                                std::uint64_t current_tick)
{
    if (!initialized_ || epoch != nextEpoch_ ||
        epoch == std::numeric_limits<std::uint64_t>::max()) {
        markFailed(2001, "local epoch sequence mismatch");
        return false;
    }
    if (epoch + 1 > std::numeric_limits<std::uint64_t>::max() /
                        config_.syncPeriodTicks) {
        markFailed(2005, "expected barrier tick overflow");
        return false;
    }
    const std::uint64_t expectedTick = (epoch + 1) * config_.syncPeriodTicks;
    if (current_tick != expectedTick) {
        markFailed(2002, "process reached barrier at an unexpected tick");
        return false;
    }
    if (!lockControl())
        return false;
    if (!checkRunningState()) {
        unlockControl();
        return false;
    }

    SharedProcessStatus &self = control_->processes[config_.processId];
    self.state = static_cast<std::uint32_t>(ProcessState::AtBarrier);
    self.currentTick = current_tick;
    self.localEpoch = epoch;
    ++self.heartbeat;
    pthread_cond_broadcast(&control_->cond);
    const struct timespec deadline = deadlineAfter(config_.barrierTimeoutSeconds);

    if (isSocProcess()) {
        while (!allProcessesAtBarrier(epoch) && checkRunningState()) {
            if (!waitWithTimeout(deadline)) {
                failLocked(2003, "barrier timeout");
                unlockControl();
                return false;
            }
        }
        if (!checkRunningState()) {
            unlockControl();
            return false;
        }
        control_->completedEpoch = epoch + 1;
        for (std::uint32_t id = 0; id < config_.processCount; ++id)
            control_->processes[id].state = static_cast<std::uint32_t>(ProcessState::Running);
        pthread_cond_broadcast(&control_->cond);
        log("commit epoch=" + std::to_string(epoch));
    } else {
        while (control_->completedEpoch < epoch + 1 && checkRunningState()) {
            if (!waitWithTimeout(deadline)) {
                failLocked(2004, "barrier timeout waiting for Coordinator");
                unlockControl();
                return false;
            }
        }
        if (!checkRunningState()) {
            unlockControl();
            return false;
        }
    }

    ++nextEpoch_;
    unlockControl();
    return true;
}

void
WiepProcessManager::markFinished()
{
    if (!initialized_ || finished_ || !lockControl())
        return;
    control_->processes[config_.processId].state =
        static_cast<std::uint32_t>(ProcessState::Finished);
    finished_ = true;
    pthread_cond_broadcast(&control_->cond);

    if (allProcessesFinished()) {
        control_->globalState = static_cast<std::uint32_t>(GlobalState::Finished);
        pthread_cond_broadcast(&control_->cond);
    } else if (isSocProcess()) {
        const struct timespec deadline = deadlineAfter(config_.barrierTimeoutSeconds);
        while (!allProcessesFinished() &&
               control_->globalState != static_cast<std::uint32_t>(GlobalState::Failed)) {
            if (!waitWithTimeout(deadline)) {
                failLocked(3001, "timeout waiting for processes to finish");
                break;
            }
        }
        if (allProcessesFinished()) {
            control_->globalState = static_cast<std::uint32_t>(GlobalState::Finished);
            pthread_cond_broadcast(&control_->cond);
        }
    }
    unlockControl();
    log("Finished");
}

void
WiepProcessManager::markFailed(std::int32_t error_code,
                               const std::string &reason)
{
    if (!initialized_ || !lockControl())
        return;
    failLocked(error_code, reason);
    unlockControl();
}

void
WiepProcessManager::updateHeartbeat(std::uint64_t current_tick)
{
    if (!initialized_ || !lockControl())
        return;
    SharedProcessStatus &self = control_->processes[config_.processId];
    self.currentTick = current_tick;
    ++self.heartbeat;
    unlockControl();
}

bool
WiepProcessManager::terminateRequested() const
{
    if (!initialized_ || !lockControl())
        return true;
    const bool requested = control_->terminateRequested != 0;
    unlockControl();
    return requested;
}

bool
WiepProcessManager::lockControl() const
{
    if (control_ == NULL)
        return false;
    const int result = pthread_mutex_lock(&control_->mutex);
#if defined(__linux__)
    if (result == EOWNERDEAD) {
        pthread_mutex_consistent(&control_->mutex);
        control_->globalState = static_cast<std::uint32_t>(GlobalState::Failed);
        control_->terminateRequested = 1;
        std::snprintf(control_->failureReason,
                      sizeof(control_->failureReason),
                      "%s", "process died while holding control mutex");
        pthread_cond_broadcast(&control_->cond);
        std::cerr << "[WiepMp][P" << config_.processId
                  << "] detected dead control-mutex owner\n";
        return true;
    }
#endif
    if (result != 0) {
        std::cerr << "[WiepMp][P" << config_.processId
                  << "] pthread_mutex_lock failed: " << std::strerror(result)
                  << "\n";
        return false;
    }
    return true;
}

void
WiepProcessManager::unlockControl() const
{
    pthread_mutex_unlock(&control_->mutex);
}

bool
WiepProcessManager::waitWithTimeout(const struct timespec &deadline) const
{
    const int result = pthread_cond_timedwait(&control_->cond,
                                              &control_->mutex, &deadline);
    if (result == 0)
        return true;
    if (result != ETIMEDOUT) {
        std::cerr << "[WiepMp][P" << config_.processId
                  << "] pthread_cond_timedwait failed: "
                  << std::strerror(result) << "\n";
    }
    return false;
}

bool
WiepProcessManager::allProcessesReady() const
{
    if (control_->registeredCount != config_.processCount ||
        control_->readyCount != config_.processCount)
        return false;
    for (std::uint32_t id = 0; id < config_.processCount; ++id) {
        if (control_->processes[id].state !=
            static_cast<std::uint32_t>(ProcessState::Ready))
            return false;
    }
    return true;
}

bool
WiepProcessManager::allProcessesAtBarrier(std::uint64_t epoch) const
{
    for (std::uint32_t id = 0; id < config_.processCount; ++id) {
        const SharedProcessStatus &status = control_->processes[id];
        if (status.state != static_cast<std::uint32_t>(ProcessState::AtBarrier) ||
            status.localEpoch != epoch ||
            status.currentTick != (epoch + 1) * config_.syncPeriodTicks)
            return false;
    }
    return true;
}

bool
WiepProcessManager::allProcessesFinished() const
{
    for (std::uint32_t id = 0; id < config_.processCount; ++id) {
        if (control_->processes[id].state !=
            static_cast<std::uint32_t>(ProcessState::Finished))
            return false;
    }
    return true;
}

bool
WiepProcessManager::checkRunningState() const
{
    return control_->globalState == static_cast<std::uint32_t>(GlobalState::Running) &&
           control_->terminateRequested == 0;
}

void
WiepProcessManager::failLocked(std::int32_t error_code,
                               const std::string &reason)
{
    SharedProcessStatus &self = control_->processes[config_.processId];
    self.state = static_cast<std::uint32_t>(ProcessState::Failed);
    self.errorCode = error_code;
    control_->globalState = static_cast<std::uint32_t>(GlobalState::Failed);
    control_->terminateRequested = 1;
    std::snprintf(control_->failureReason, sizeof(control_->failureReason),
                  "%s", reason.c_str());
    pthread_cond_broadcast(&control_->cond);
    std::cerr << "[WiepMp][P" << config_.processId << "] Failed(" << error_code
              << "): " << reason << "\n";
    for (std::uint32_t id = 0; id < config_.processCount; ++id) {
        const SharedProcessStatus &status = control_->processes[id];
        std::cerr << "  P" << id << " pid=" << status.pid
                  << " state="
                  << processStateName(static_cast<ProcessState>(status.state))
                  << " epoch=" << status.localEpoch
                  << " tick=" << status.currentTick
                  << " heartbeat=" << status.heartbeat << "\n";
    }
}

void
WiepProcessManager::dumpProcessStatus() const
{
    if (!initialized_ || !lockControl())
        return;
    std::cerr << "[WiepMp] session=" << config_.sessionName
              << " completedEpoch=" << control_->completedEpoch << "\n";
    for (std::uint32_t id = 0; id < config_.processCount; ++id) {
        const SharedProcessStatus &status = control_->processes[id];
        std::cerr << "  P" << id << " name=" << status.name
                  << " pid=" << status.pid << " state="
                  << processStateName(static_cast<ProcessState>(status.state))
                  << " epoch=" << status.localEpoch
                  << " tick=" << status.currentTick << "\n";
    }
    unlockControl();
}

void
WiepProcessManager::log(const std::string &message) const
{
    if (config_.debug)
        std::cout << "[WiepMp][P" << config_.processId << "] "
                  << message << "\n";
}

struct timespec
WiepProcessManager::deadlineAfter(std::uint32_t seconds) const
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += seconds;
    return deadline;
}

} // namespace WiepMp
