#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "base/types.hh"
#include "mem/packet.hh"
#include "sim/sim_object.hh"

#include "params/BankAddressMapper.hh"

namespace gem5::customxbar
{

/**
 * Converts a packet address into a logical bank, a physical bank, and the
 * per-layer global output ids needed to reach that physical bank.
 */
class BankAddressMapper : public SimObject
{
  public:
    struct Mapping
    {
        std::uint32_t logicalBank;
        std::uint32_t physicalBank;
        std::vector<std::uint32_t> routeOutputs;
    };

    explicit BankAddressMapper(const BankAddressMapperParams &p);

    Mapping map(PacketPtr pkt, std::size_t ingress_id) const;
    std::uint32_t physicalBankFor(Addr address) const;

  private:
    std::uint32_t logicalBankFor(Addr address) const;

    const Addr baseAddr;
    const Addr logicalBankSize;
    const std::size_t logicalBankCount;
    const std::size_t physicalBanksPerLogical;
    const Addr interleaveSize;
    const std::size_t physicalBankCount;
    const std::size_t ingressCount;
    const std::size_t routeLayers;
    const std::vector<std::uint32_t> logicalToPhysical;
    const std::vector<std::uint32_t> routeTable;
};

} // namespace gem5::customxbar
