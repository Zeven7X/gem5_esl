#include "wiep/mp_sync/posix_ipc.hh"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#include "wiep/mp_sync/common.hh"

namespace wiep
{
namespace mp
{

namespace
{

std::string
systemError(const std::string &operation, const std::string &name)
{
    return operation + " " + name + " failed: " + std::strerror(errno);
}

} // namespace

std::string
makeIpcName(const std::string &session, const std::string &kind,
            std::uint64_t id)
{
    std::ostringstream stream;
    stream << "/wmp_" << std::hex
           << static_cast<std::uint32_t>(stableHash(session)) << "_"
           << static_cast<std::uint32_t>(stableHash(kind)) << "_" << id;
    return stream.str();
}

SharedMemoryRegion::SharedMemoryRegion(const std::string &name,
                                       std::size_t size, bool create)
    : name_(name), size_(size), owner_(create)
{
    if (create) {
        fd_ = shm_open(name_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd_ < 0)
            throw MpError(systemError("shm_open(create)", name_));
        if (ftruncate(fd_, static_cast<off_t>(size_)) != 0)
            throw MpError(systemError("ftruncate", name_));
    } else {
        for (unsigned retry = 0; retry < 500; ++retry) {
            fd_ = shm_open(name_.c_str(), O_RDWR, 0600);
            if (fd_ >= 0)
                break;
            if (errno != ENOENT)
                throw MpError(systemError("shm_open", name_));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (fd_ < 0)
            throw MpError("timed out opening shared memory " + name_);
    }

    address_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED,
                    fd_, 0);
    if (address_ == MAP_FAILED) {
        address_ = nullptr;
        throw MpError(systemError("mmap", name_));
    }
}

SharedMemoryRegion::~SharedMemoryRegion()
{
    if (address_)
        munmap(address_, size_);
    if (fd_ >= 0)
        close(fd_);
    if (owner_)
        shm_unlink(name_.c_str());
}

NamedSemaphore::NamedSemaphore(const std::string &name, bool create,
                               unsigned initial_value)
    : name_(name), owner_(create)
{
    if (create) {
        sem_ = sem_open(name_.c_str(), O_CREAT | O_EXCL, 0600,
                        initial_value);
        if (sem_ == SEM_FAILED)
            throw MpError(systemError("sem_open(create)", name_));
    } else {
        for (unsigned retry = 0; retry < 500; ++retry) {
            sem_ = sem_open(name_.c_str(), 0);
            if (sem_ != SEM_FAILED)
                break;
            if (errno != ENOENT)
                throw MpError(systemError("sem_open", name_));
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (sem_ == SEM_FAILED)
            throw MpError("timed out opening semaphore " + name_);
    }
}

NamedSemaphore::~NamedSemaphore()
{
    if (sem_ != SEM_FAILED)
        sem_close(sem_);
    if (owner_)
        sem_unlink(name_.c_str());
}

void
NamedSemaphore::post()
{
    if (sem_post(sem_) != 0)
        throw MpError(systemError("sem_post", name_));
}

void
NamedSemaphore::wait()
{
    while (sem_wait(sem_) != 0) {
        if (errno != EINTR)
            throw MpError(systemError("sem_wait", name_));
    }
}

} // namespace mp
} // namespace wiep
