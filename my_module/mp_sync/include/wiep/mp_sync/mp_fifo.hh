#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <string>
#include <type_traits>

#include "wiep/mp_sync/common.hh"
#include "wiep/mp_sync/posix_ipc.hh"

namespace wiep
{
namespace mp
{

template <typename T, std::size_t DEPTH, std::size_t MAX_BURST = DEPTH>
class MpFifo : public IMpSyncChannel
{
    static_assert(std::is_trivially_copyable<T>::value,
                  "MpFifo requires a trivially copyable wire type");
    static_assert(!std::is_pointer<T>::value,
                  "MpFifo cannot transfer a process-local pointer");
    static_assert(DEPTH > 0, "MpFifo DEPTH must be non-zero");
    static_assert(MAX_BURST > 0 && MAX_BURST <= DEPTH,
                  "MpFifo MAX_BURST must be in [1, DEPTH]");

    struct ChannelMeta
    {
        std::uint32_t magic;
        std::uint32_t version;
        std::uint64_t type_hash;
        std::uint32_t elem_size;
        std::uint32_t depth;
        std::uint32_t max_burst;
        std::uint32_t direction;
    };

    struct alignas(64) DataSlot
    {
        std::uint64_t sync_id;
        std::uint32_t count;
        std::uint32_t reserved;
        alignas(T) std::array<unsigned char, sizeof(T) * MAX_BURST> payload;
    };

    struct alignas(64) CreditSlot
    {
        std::uint64_t sync_id;
        std::uint32_t read_count;
        std::uint32_t reserved;
    };

    struct SharedChannel
    {
        ChannelMeta meta;
        DataSlot data_slots[2];
        CreditSlot credit_slots[2];
    };

  public:
    MpFifo(const std::string &session, const std::string &channel_name,
           const std::string &type_name, MpDirection direction, MpRole role,
           std::uint32_t worker_id = 0)
        : name_(channel_name), direction_(direction), role_(role),
          workerId_(worker_id), region_(
              makeIpcName(session, "channel_" + channel_name, worker_id),
              sizeof(SharedChannel), role == MpRole::Main),
          shared_(static_cast<SharedChannel *>(region_.data()))
    {
        if (role_ == MpRole::Main)
            initialize(type_name);
        else
            validate(type_name);
    }

    bool nbWrite(const T &data)
    {
        if (!isWriter())
            return false;
        if (remoteOccupancy_ >= DEPTH || pendingWrites_.size() >= MAX_BURST)
            return false;

        pendingWrites_.push_back(data);
        ++remoteOccupancy_;
        return true;
    }

    bool nbRead(T &data)
    {
        if (!nbGet(data))
            return false;
        return delTrf();
    }

    bool nbGet(T &data) const
    {
        if (!isReader() || visibleReads_.empty())
            return false;
        data = visibleReads_.front();
        return true;
    }

    T &nbGet()
    {
        if (!isReader() || visibleReads_.empty())
            throw MpError("MpFifo " + name_ + " has no visible data");
        return visibleReads_.front();
    }

    bool delTrf()
    {
        if (!isReader() || visibleReads_.empty())
            return false;
        visibleReads_.pop_front();
        ++pendingReadCount_;
        return true;
    }

    T &front() { return nbGet(); }
    T &back()
    {
        if (!isReader() || visibleReads_.empty())
            throw MpError("MpFifo " + name_ + " has no visible data");
        return visibleReads_.back();
    }
    T &operator[](std::size_t index)
    {
        if (!isReader() || index >= visibleReads_.size())
            throw MpError("MpFifo " + name_ + " index is out of range");
        return visibleReads_[index];
    }

    bool canPop() const { return isReader() && !visibleReads_.empty(); }
    bool empty() const { return visibleReads_.empty(); }
    bool full() const { return isWriter() && remoteOccupancy_ >= DEPTH; }
    std::size_t size() const { return visibleReads_.size(); }
    std::size_t emptyNum() const
    {
        return isWriter() ? DEPTH - remoteOccupancy_ : 0;
    }
    std::size_t numAvailable() const { return visibleReads_.size(); }
    std::size_t numFree() const { return emptyNum(); }

    void beginInterval(std::uint64_t sync_id) override
    {
        const std::size_t slot = sync_id & 1U;
        if (isWriter()) {
            const CreditSlot &credit = shared_->credit_slots[slot];
            checkSyncId(credit.sync_id, sync_id, "credit");
            if (credit.read_count > remoteOccupancy_)
                throw MpError("MpFifo " + name_ + " credit underflow");
            remoteOccupancy_ -= credit.read_count;
        } else {
            const DataSlot &data = shared_->data_slots[slot];
            checkSyncId(data.sync_id, sync_id, "data");
            if (data.count > MAX_BURST ||
                visibleReads_.size() + data.count > DEPTH) {
                throw MpError("MpFifo " + name_ + " receive overflow");
            }
            for (std::size_t index = 0; index < data.count; ++index) {
                T item;
                std::memcpy(&item,
                            data.payload.data() + index * sizeof(T),
                            sizeof(T));
                visibleReads_.push_back(item);
            }
        }
    }

    void endInterval(std::uint64_t sync_id) override
    {
        if (sync_id == std::numeric_limits<std::uint64_t>::max())
            throw MpError("MpFifo sync_id overflow");

        const std::uint64_t next_sync_id = sync_id + 1;
        const std::size_t slot = next_sync_id & 1U;
        if (isWriter()) {
            DataSlot &data = shared_->data_slots[slot];
            data.count = static_cast<std::uint32_t>(pendingWrites_.size());
            for (std::size_t index = 0; index < pendingWrites_.size(); ++index) {
                std::memcpy(data.payload.data() + index * sizeof(T),
                            &pendingWrites_[index], sizeof(T));
            }
            data.sync_id = next_sync_id;
            pendingWrites_.clear();
        } else {
            CreditSlot &credit = shared_->credit_slots[slot];
            credit.read_count = pendingReadCount_;
            credit.sync_id = next_sync_id;
            pendingReadCount_ = 0;
        }
    }

    const std::string &name() const override { return name_; }
    std::uint32_t workerId() const override { return workerId_; }

  private:
    bool isWriter() const
    {
        return (direction_ == MpDirection::MainToWorker &&
                role_ == MpRole::Main) ||
               (direction_ == MpDirection::WorkerToMain &&
                role_ == MpRole::Worker);
    }

    bool isReader() const { return !isWriter(); }

    void initialize(const std::string &type_name)
    {
        std::memset(shared_, 0, sizeof(*shared_));
        shared_->meta.magic = ChannelMagic;
        shared_->meta.version = ProtocolVersion;
        shared_->meta.type_hash = stableHash(type_name);
        shared_->meta.elem_size = sizeof(T);
        shared_->meta.depth = DEPTH;
        shared_->meta.max_burst = MAX_BURST;
        shared_->meta.direction = static_cast<std::uint32_t>(direction_);
        shared_->data_slots[0].sync_id = 0;
        shared_->credit_slots[0].sync_id = 0;
        shared_->data_slots[1].sync_id =
            std::numeric_limits<std::uint64_t>::max();
        shared_->credit_slots[1].sync_id =
            std::numeric_limits<std::uint64_t>::max();
    }

    void validate(const std::string &type_name) const
    {
        const ChannelMeta &meta = shared_->meta;
        if (meta.magic != ChannelMagic || meta.version != ProtocolVersion ||
            meta.type_hash != stableHash(type_name) ||
            meta.elem_size != sizeof(T) || meta.depth != DEPTH ||
            meta.max_burst != MAX_BURST ||
            meta.direction != static_cast<std::uint32_t>(direction_)) {
            throw MpError("MpFifo metadata mismatch for channel " + name_);
        }
    }

    void checkSyncId(std::uint64_t received, std::uint64_t expected,
                     const char *lane) const
    {
        if (received != expected) {
            throw MpError("MpFifo " + name_ + " " + lane +
                          " sync_id mismatch: expected " +
                          std::to_string(expected) + ", received " +
                          std::to_string(received));
        }
    }

    std::string name_;
    MpDirection direction_;
    MpRole role_;
    std::uint32_t workerId_;
    SharedMemoryRegion region_;
    SharedChannel *shared_;
    std::deque<T> pendingWrites_;
    std::deque<T> visibleReads_;
    std::uint32_t pendingReadCount_ = 0;
    std::size_t remoteOccupancy_ = 0;
};

} // namespace mp
} // namespace wiep
