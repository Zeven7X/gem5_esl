#include "simple_bus.hh"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace example
{

namespace
{

std::string hex32(std::uint32_t value)
{
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

} // namespace

void
SimpleBus::addDevice(MemoryMappedDevice *device)
{
    devices.push_back(device);
}

void
SimpleBus::write(std::uint32_t address, std::uint32_t value)
{
    MemoryMappedDevice *device = findDevice(address);
    if (!device) {
        throw std::out_of_range("bus write to unmapped address " + hex32(address));
    }

    std::cout << "[bus ] write " << hex32(value)
              << " -> " << hex32(address)
              << " (" << device->name() << ")\n";
    device->write(address, value);
}

std::uint32_t
SimpleBus::read(std::uint32_t address) const
{
    MemoryMappedDevice *device = findDevice(address);
    if (!device) {
        throw std::out_of_range("bus read from unmapped address " + hex32(address));
    }

    const std::uint32_t value = device->read(address);
    std::cout << "[bus ] read  " << hex32(value)
              << " <- " << hex32(address)
              << " (" << device->name() << ")\n";
    return value;
}

MemoryMappedDevice *
SimpleBus::findDevice(std::uint32_t address) const
{
    for (MemoryMappedDevice *device : devices) {
        if (device->contains(address)) {
            return device;
        }
    }

    return nullptr;
}

Sram::Sram(std::uint32_t baseAddress, std::size_t sizeInBytes, std::string label)
    : base(baseAddress), bytes(sizeInBytes, 0), label(std::move(label))
{
    if (sizeInBytes < sizeof(std::uint32_t)) {
        throw std::invalid_argument("SRAM size must be at least 4 bytes");
    }
}

bool
Sram::contains(std::uint32_t address) const
{
    return address >= base && address < base + bytes.size();
}

void
Sram::write(std::uint32_t address, std::uint32_t value)
{
    const std::size_t offset = offsetOf(address);
    if (offset + sizeof(std::uint32_t) > bytes.size()) {
        throw std::out_of_range("SRAM word write crosses the end of memory");
    }

    bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

std::uint32_t
Sram::read(std::uint32_t address) const
{
    const std::size_t offset = offsetOf(address);
    if (offset + sizeof(std::uint32_t) > bytes.size()) {
        throw std::out_of_range("SRAM word read crosses the end of memory");
    }

    return static_cast<std::uint32_t>(bytes[offset + 0])
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

std::string
Sram::name() const
{
    return label;
}

std::optional<std::uint32_t>
Sram::peekWord(std::uint32_t address) const
{
    if (!contains(address)) {
        return std::nullopt;
    }

    const std::size_t offset = static_cast<std::size_t>(address - base);
    if (offset + sizeof(std::uint32_t) > bytes.size()) {
        return std::nullopt;
    }

    return read(address);
}

std::size_t
Sram::offsetOf(std::uint32_t address) const
{
    if (!contains(address)) {
        throw std::out_of_range("address is outside of SRAM");
    }

    return static_cast<std::size_t>(address - base);
}

Stimulus::Stimulus(SimpleBus &bus) : bus(bus)
{
}

void
Stimulus::run() const
{
    std::cout << "[stim] start\n";

    const std::vector<Transaction> writes = {
        {0x00000000U, 0x11223344U},
        {0x00000004U, 0xa5a55a5aU},
        {0x00000008U, 0xdeadbeefU},
    };

    for (const Transaction &tx : writes) {
        std::cout << "[stim] issue write addr=" << hex32(tx.address)
                  << " data=" << hex32(tx.value) << "\n";
        bus.write(tx.address, tx.value);
    }

    for (const Transaction &tx : writes) {
        const std::uint32_t value = bus.read(tx.address);
        std::cout << "[stim] check read  addr=" << hex32(tx.address)
                  << " data=" << hex32(value)
                  << (value == tx.value ? " [ok]\n" : " [mismatch]\n");
    }

    std::cout << "[stim] finish\n";
}

} // namespace example
