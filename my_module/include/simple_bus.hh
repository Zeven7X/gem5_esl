#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace example
{

struct Transaction
{
    std::uint32_t address = 0;
    std::uint32_t value = 0;
};

class MemoryMappedDevice
{
  public:
    virtual ~MemoryMappedDevice() = default;

    virtual bool contains(std::uint32_t address) const = 0;
    virtual void write(std::uint32_t address, std::uint32_t value) = 0;
    virtual std::uint32_t read(std::uint32_t address) const = 0;
    virtual std::string name() const = 0;
};

class SimpleBus
{
  public:
    void addDevice(MemoryMappedDevice *device);
    void write(std::uint32_t address, std::uint32_t value);
    std::uint32_t read(std::uint32_t address) const;

  private:
    MemoryMappedDevice *findDevice(std::uint32_t address) const;

    std::vector<MemoryMappedDevice *> devices;
};

class Sram : public MemoryMappedDevice
{
  public:
    Sram(std::uint32_t baseAddress, std::size_t sizeInBytes, std::string label);

    bool contains(std::uint32_t address) const override;
    void write(std::uint32_t address, std::uint32_t value) override;
    std::uint32_t read(std::uint32_t address) const override;
    std::string name() const override;

    std::optional<std::uint32_t> peekWord(std::uint32_t address) const;

  private:
    std::size_t offsetOf(std::uint32_t address) const;

    std::uint32_t base = 0;
    std::vector<std::uint8_t> bytes;
    std::string label;
};

class Stimulus
{
  public:
    explicit Stimulus(SimpleBus &bus);

    void run() const;

  private:
    SimpleBus &bus;
};

} // namespace example
