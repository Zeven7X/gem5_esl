#ifndef WIEP_SHARED_CHANNEL_HH
#define WIEP_SHARED_CHANNEL_HH

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <type_traits>
#include <unistd.h>

#include "WiepMpCommon.hh"

namespace WiepMp
{

struct SharedChannelConfig
{
    std::string sessionName;
    std::uint32_t channelId;
    std::uint32_t processId;
    std::uint32_t processA;
    std::uint32_t processB;
    std::uint32_t attachTimeoutSeconds;
    bool unlinkOnExit;
    bool debug;

    SharedChannelConfig()
        : channelId(0), processId(0), processA(0), processB(1),
          attachTimeoutSeconds(60), unlinkOnExit(true), debug(false)
    {
    }
};

template <typename T, std::uint32_t Capacity = 256>
class WiepSharedChannel
{
    static_assert(std::is_trivially_copyable<T>::value,
                  "WiepSharedChannel requires a trivially copyable type");
    static_assert(!std::is_pointer<T>::value,
                  "process-local pointers cannot cross shared memory");
    static_assert(Capacity >= 2,
                  "Capacity must be at least 2; usable depth is Capacity - 1");

    struct alignas(64) SharedRingBuffer
    {
        std::uint32_t readIndex;
        std::uint32_t writeIndex;
        alignas(T) unsigned char data[sizeof(T) * Capacity];
    };

    struct SharedChannelStorage
    {
        std::uint64_t magic;
        std::uint32_t version;
        std::uint32_t initialized;
        std::uint32_t channelId;
        std::uint32_t processA;
        std::uint32_t processB;
        std::uint32_t capacity;
        std::uint32_t elementSize;
        SharedRingBuffer ringAB;
        SharedRingBuffer ringBA;
    };

  public:
    explicit WiepSharedChannel(const SharedChannelConfig &config)
        : config_(config), shmName_(channelShmName(config.sessionName,
                                                   config.channelId)),
          shmFd_(-1), shared_(NULL), txRing_(NULL), rxRing_(NULL),
          owner_(false), valid_(false)
    {
    }

    ~WiepSharedChannel()
    {
        if (shared_ != NULL)
            munmap(shared_, sizeof(SharedChannelStorage));
        if (shmFd_ >= 0)
            close(shmFd_);
        if (owner_ && config_.unlinkOnExit)
            shm_unlink(shmName_.c_str());
    }

    WiepSharedChannel(const WiepSharedChannel &) = delete;
    WiepSharedChannel &operator=(const WiepSharedChannel &) = delete;

    bool initialize()
    {
        if (valid_)
            return true;
        if (config_.processA == config_.processB ||
            (config_.processId != config_.processA &&
             config_.processId != config_.processB) ||
            config_.sessionName.empty()) {
            std::cerr << "[WiepMp][Channel " << config_.channelId
                      << "] invalid configuration\n";
            return false;
        }

        const std::uint32_t creator = config_.processA < config_.processB
            ? config_.processA : config_.processB;
        if (config_.processId == creator) {
            if (!createStorage())
                return false;
        } else if (!attachStorage()) {
            return false;
        }

        if (!validateStorage())
            return false;
        if (config_.processId == config_.processA) {
            txRing_ = &shared_->ringAB;
            rxRing_ = &shared_->ringBA;
        } else {
            txRing_ = &shared_->ringBA;
            rxRing_ = &shared_->ringAB;
        }
        valid_ = true;
        log("initialized");
        return true;
    }

    bool nbWrite(const T &value)
    {
        if (!valid_)
            return false;
        const std::uint32_t writeIndex =
            __atomic_load_n(&txRing_->writeIndex, __ATOMIC_RELAXED);
        const std::uint32_t nextWrite = nextIndex(writeIndex);
        const std::uint32_t readIndex =
            __atomic_load_n(&txRing_->readIndex, __ATOMIC_ACQUIRE);
        if (nextWrite == readIndex)
            return false;

        std::memcpy(slotAddress(txRing_, writeIndex), &value, sizeof(T));
        __atomic_store_n(&txRing_->writeIndex, nextWrite, __ATOMIC_RELEASE);
        return true;
    }

    bool nbRead(T &value)
    {
        if (!nbGet(value))
            return false;
        return delTrf();
    }

    bool nbGet(T &value) const
    {
        if (!valid_)
            return false;
        const std::uint32_t readIndex =
            __atomic_load_n(&rxRing_->readIndex, __ATOMIC_RELAXED);
        const std::uint32_t writeIndex =
            __atomic_load_n(&rxRing_->writeIndex, __ATOMIC_ACQUIRE);
        if (readIndex == writeIndex)
            return false;
        std::memcpy(&value, slotAddress(rxRing_, readIndex), sizeof(T));
        return true;
    }

    bool delTrf()
    {
        if (!valid_)
            return false;
        const std::uint32_t readIndex =
            __atomic_load_n(&rxRing_->readIndex, __ATOMIC_RELAXED);
        const std::uint32_t writeIndex =
            __atomic_load_n(&rxRing_->writeIndex, __ATOMIC_ACQUIRE);
        if (readIndex == writeIndex)
            return false;
        __atomic_store_n(&rxRing_->readIndex, nextIndex(readIndex),
                         __ATOMIC_RELEASE);
        return true;
    }

    bool canRead() const { return valid_ && rxSize() != 0; }
    bool canPop() const { return canRead(); }
    bool empty() const { return !canRead(); }
    bool full() const { return valid_ && txFreeSize() == 0; }

    std::uint32_t rxSize() const
    {
        return valid_ ? ringSize(rxRing_) : 0;
    }

    std::uint32_t txSize() const
    {
        return valid_ ? ringSize(txRing_) : 0;
    }

    std::uint32_t txFreeSize() const
    {
        return valid_ ? (Capacity - 1U) - txSize() : 0;
    }

    std::uint32_t size() const { return rxSize(); }
    std::uint32_t emptyNum() const { return txFreeSize(); }
    std::uint32_t channelId() const { return config_.channelId; }
    std::uint32_t capacity() const { return Capacity; }
    std::uint32_t usableCapacity() const { return Capacity - 1U; }
    bool valid() const { return valid_; }

  private:
    bool createStorage()
    {
        shmFd_ = shm_open(shmName_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
        if (shmFd_ < 0) {
            printSystemError("shm_open(create)");
            return false;
        }
        owner_ = true;
        if (ftruncate(shmFd_, sizeof(SharedChannelStorage)) != 0) {
            printSystemError("ftruncate");
            return false;
        }
        if (!mapStorage())
            return false;

        std::memset(shared_, 0, sizeof(*shared_));
        shared_->magic = ChannelMagic;
        shared_->version = ProtocolVersion;
        shared_->channelId = config_.channelId;
        shared_->processA = config_.processA;
        shared_->processB = config_.processB;
        shared_->capacity = Capacity;
        shared_->elementSize = sizeof(T);
        __atomic_store_n(&shared_->initialized, 1U, __ATOMIC_RELEASE);
        return true;
    }

    bool attachStorage()
    {
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(config_.attachTimeoutSeconds);
        while (std::chrono::steady_clock::now() < deadline) {
            shmFd_ = shm_open(shmName_.c_str(), O_RDWR, 0600);
            if (shmFd_ >= 0)
                break;
            if (errno != ENOENT) {
                printSystemError("shm_open");
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (shmFd_ < 0) {
            std::cerr << "[WiepMp][Channel " << config_.channelId
                      << "] attach timeout\n";
            return false;
        }
        if (!mapStorage())
            return false;
        while (std::chrono::steady_clock::now() < deadline &&
               __atomic_load_n(&shared_->initialized, __ATOMIC_ACQUIRE) == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return __atomic_load_n(&shared_->initialized, __ATOMIC_ACQUIRE) != 0;
    }

    bool mapStorage()
    {
        void *mapping = mmap(NULL, sizeof(SharedChannelStorage),
                             PROT_READ | PROT_WRITE, MAP_SHARED, shmFd_, 0);
        if (mapping == MAP_FAILED) {
            shared_ = NULL;
            printSystemError("mmap");
            return false;
        }
        shared_ = static_cast<SharedChannelStorage *>(mapping);
        return true;
    }

    bool validateStorage() const
    {
        if (shared_ == NULL || shared_->magic != ChannelMagic ||
            shared_->version != ProtocolVersion ||
            shared_->channelId != config_.channelId ||
            shared_->processA != config_.processA ||
            shared_->processB != config_.processB ||
            shared_->capacity != Capacity ||
            shared_->elementSize != sizeof(T)) {
            std::cerr << "[WiepMp][Channel " << config_.channelId
                      << "] shared metadata mismatch\n";
            return false;
        }
        return true;
    }

    static std::uint32_t nextIndex(std::uint32_t index)
    {
        ++index;
        return index == Capacity ? 0 : index;
    }

    static unsigned char *slotAddress(SharedRingBuffer *ring,
                                      std::uint32_t index)
    {
        return ring->data + sizeof(T) * index;
    }

    static const unsigned char *slotAddress(const SharedRingBuffer *ring,
                                            std::uint32_t index)
    {
        return ring->data + sizeof(T) * index;
    }

    static std::uint32_t ringSize(const SharedRingBuffer *ring)
    {
        const std::uint32_t readIndex =
            __atomic_load_n(&ring->readIndex, __ATOMIC_ACQUIRE);
        const std::uint32_t writeIndex =
            __atomic_load_n(&ring->writeIndex, __ATOMIC_ACQUIRE);
        return writeIndex >= readIndex
            ? writeIndex - readIndex
            : Capacity - readIndex + writeIndex;
    }

    void printSystemError(const char *operation) const
    {
        std::cerr << "[WiepMp][Channel " << config_.channelId << "] "
                  << operation << " failed: " << std::strerror(errno) << "\n";
    }

    void log(const std::string &message) const
    {
        if (config_.debug)
            std::cout << "[WiepMp][P" << config_.processId << "][Channel "
                      << config_.channelId << "] " << message << "\n";
    }

    SharedChannelConfig config_;
    std::string shmName_;
    int shmFd_;
    SharedChannelStorage *shared_;
    SharedRingBuffer *txRing_;
    SharedRingBuffer *rxRing_;
    bool owner_;
    bool valid_;
};

} // namespace WiepMp

#endif // WIEP_SHARED_CHANNEL_HH
