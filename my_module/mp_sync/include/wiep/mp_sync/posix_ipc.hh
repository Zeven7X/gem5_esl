#pragma once

#include <cstddef>
#include <cstdint>
#include <semaphore.h>
#include <string>

namespace wiep
{
namespace mp
{

std::string makeIpcName(const std::string &session,
                        const std::string &kind,
                        std::uint64_t id = 0);

class SharedMemoryRegion
{
  public:
    SharedMemoryRegion(const std::string &name, std::size_t size,
                       bool create);
    ~SharedMemoryRegion();

    SharedMemoryRegion(const SharedMemoryRegion &) = delete;
    SharedMemoryRegion &operator=(const SharedMemoryRegion &) = delete;

    void *data() const { return address_; }
    std::size_t size() const { return size_; }

  private:
    std::string name_;
    std::size_t size_ = 0;
    int fd_ = -1;
    void *address_ = nullptr;
    bool owner_ = false;
};

class NamedSemaphore
{
  public:
    NamedSemaphore(const std::string &name, bool create,
                   unsigned initial_value = 0);
    ~NamedSemaphore();

    NamedSemaphore(const NamedSemaphore &) = delete;
    NamedSemaphore &operator=(const NamedSemaphore &) = delete;

    void post();
    void wait();

  private:
    std::string name_;
    sem_t *sem_ = SEM_FAILED;
    bool owner_ = false;
};

} // namespace mp
} // namespace wiep
