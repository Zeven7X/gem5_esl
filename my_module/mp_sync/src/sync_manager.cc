#include "wiep/mp_sync/sync_manager.hh"

#include <cstring>

namespace wiep
{
namespace mp
{

MpSyncManager::MpSyncManager(const std::string &session, MpRole role,
                             std::uint32_t worker_count,
                             std::uint32_t worker_id)
    : session_(session), role_(role), workerCount_(worker_count),
      workerId_(worker_id), controlRegion_(
          makeIpcName(session, "control"), sizeof(ControlBlock),
          role == MpRole::Main),
      control_(static_cast<ControlBlock *>(controlRegion_.data()))
{
    if (workerCount_ == 0 || workerCount_ > MaxWorkers)
        throw MpError("worker_count is outside the supported range");
    if (role_ == MpRole::Worker && workerId_ >= workerCount_)
        throw MpError("worker_id is outside the supported range");

    if (role_ == MpRole::Main)
        initializeControl();
    else
        validateControl();

    const std::uint32_t first = role_ == MpRole::Main ? 0 : workerId_;
    const std::uint32_t last = role_ == MpRole::Main
        ? workerCount_ : workerId_ + 1;
    for (std::uint32_t id = first; id < last; ++id) {
        startSemaphores_.push_back(std::make_unique<NamedSemaphore>(
            makeIpcName(session_, "start", id), role_ == MpRole::Main));
        doneSemaphores_.push_back(std::make_unique<NamedSemaphore>(
            makeIpcName(session_, "done", id), role_ == MpRole::Main));
    }
}

MpSyncManager::~MpSyncManager() = default;

void
MpSyncManager::registerChannel(IMpSyncChannel &channel)
{
    if (channel.workerId() >= workerCount_)
        throw MpError("channel " + channel.name() + " has invalid worker_id");
    if (role_ == MpRole::Worker && channel.workerId() != workerId_)
        throw MpError("worker registered a channel owned by another worker");
    channels_.push_back(&channel);
}

void
MpSyncManager::beginInterval(std::uint64_t sync_id)
{
    checkRole(MpRole::Main, "beginInterval");
    importChannels(sync_id);

    for (std::uint32_t id = 0; id < workerCount_; ++id) {
        WorkerState &state = control_->workers[id];
        state.sync_id = sync_id;
        state.error_code = 0;
        state.status = static_cast<std::uint32_t>(MpStatus::Idle);
        state.command = static_cast<std::uint32_t>(MpCommand::Run);
        startSemaphores_[id]->post();
    }
}

void
MpSyncManager::endInterval(std::uint64_t sync_id)
{
    checkRole(MpRole::Main, "endInterval");
    exportChannels(sync_id);

    for (std::uint32_t id = 0; id < workerCount_; ++id) {
        doneSemaphores_[id]->wait();
        const WorkerState &state = control_->workers[id];
        if (state.sync_id != sync_id ||
            state.status != static_cast<std::uint32_t>(MpStatus::Done)) {
            throw MpError("worker " + std::to_string(id) +
                          " failed interval " + std::to_string(sync_id));
        }
    }
}

bool
MpSyncManager::waitInterval(std::uint64_t &sync_id)
{
    checkRole(MpRole::Worker, "waitInterval");
    startSemaphores_[0]->wait();

    WorkerState &state = control_->workers[workerId_];
    const auto command = static_cast<MpCommand>(state.command);
    if (command == MpCommand::Exit) {
        state.status = static_cast<std::uint32_t>(MpStatus::Done);
        doneSemaphores_[0]->post();
        stopped_ = true;
        return false;
    }
    if (command != MpCommand::Run)
        throw MpError("worker received an invalid command");

    sync_id = state.sync_id;
    state.status = static_cast<std::uint32_t>(MpStatus::Running);
    importChannels(sync_id);
    return true;
}

void
MpSyncManager::finishInterval(std::uint64_t sync_id)
{
    checkRole(MpRole::Worker, "finishInterval");
    WorkerState &state = control_->workers[workerId_];
    if (state.sync_id != sync_id)
        throw MpError("worker finish sync_id mismatch");

    exportChannels(sync_id);
    state.status = static_cast<std::uint32_t>(MpStatus::Done);
    doneSemaphores_[0]->post();
}

void
MpSyncManager::stop()
{
    checkRole(MpRole::Main, "stop");
    if (stopped_)
        return;

    for (std::uint32_t id = 0; id < workerCount_; ++id) {
        WorkerState &state = control_->workers[id];
        state.command = static_cast<std::uint32_t>(MpCommand::Exit);
        state.status = static_cast<std::uint32_t>(MpStatus::Idle);
        startSemaphores_[id]->post();
    }
    for (auto &semaphore : doneSemaphores_)
        semaphore->wait();
    stopped_ = true;
}

void
MpSyncManager::initializeControl()
{
    std::memset(control_, 0, sizeof(*control_));
    control_->magic = ControlMagic;
    control_->version = ProtocolVersion;
    control_->worker_count = workerCount_;
}

void
MpSyncManager::validateControl() const
{
    if (control_->magic != ControlMagic ||
        control_->version != ProtocolVersion ||
        control_->worker_count != workerCount_) {
        throw MpError("sync control metadata mismatch");
    }
}

void
MpSyncManager::importChannels(std::uint64_t sync_id)
{
    for (IMpSyncChannel *channel : channels_)
        channel->beginInterval(sync_id);
}

void
MpSyncManager::exportChannels(std::uint64_t sync_id)
{
    for (IMpSyncChannel *channel : channels_)
        channel->endInterval(sync_id);
}

void
MpSyncManager::checkRole(MpRole expected, const char *operation) const
{
    if (role_ != expected)
        throw MpError(std::string(operation) + " called by the wrong role");
}

} // namespace mp
} // namespace wiep
