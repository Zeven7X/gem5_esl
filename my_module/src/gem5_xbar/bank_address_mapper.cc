#include "gem5_xbar/bank_address_mapper.hh"

#include <algorithm>
#include <limits>
#include <utility>

#include "base/logging.hh"

namespace gem5::customxbar
{

BankAddressMapper::BankAddressMapper(const BankAddressMapperParams &p)
    : SimObject(p),
      baseAddr(p.base_addr),
      logicalBankSize(p.logical_bank_size),
      logicalBankCount(p.logical_bank_count),
      physicalBanksPerLogical(p.physical_banks_per_logical),
      interleaveSize(p.interleave_size),
      physicalBankCount(p.physical_bank_count),
      ingressCount(p.ingress_count),
      routeLayers(p.route_layers),
      logicalToPhysical(p.logical_to_physical.begin(),
                        p.logical_to_physical.end()),
      routeTable(p.route_table.begin(), p.route_table.end())
{
    panic_if(logicalBankSize == 0, "%s logical_bank_size must be non-zero",
             name());
    panic_if(logicalBankCount == 0, "%s needs at least one logical bank",
             name());
    panic_if(physicalBanksPerLogical == 0,
             "%s needs at least one physical bank per logical bank", name());
    panic_if(interleaveSize == 0, "%s interleave_size must be non-zero",
             name());
    panic_if(physicalBankCount == 0, "%s needs at least one physical bank",
             name());
    panic_if(ingressCount == 0, "%s needs at least one ingress", name());
    panic_if(routeLayers == 0, "%s needs at least one route layer", name());

    const std::size_t mapping_entries =
        logicalBankCount * physicalBanksPerLogical;
    panic_if(logicalToPhysical.size() != mapping_entries,
             "%s expected %zu logical-to-physical entries, got %zu", name(),
             mapping_entries, logicalToPhysical.size());

    for (const std::uint32_t physical : logicalToPhysical) {
        panic_if(physical >= physicalBankCount,
                 "%s maps to physical bank %u but only %zu banks exist",
                 name(), physical, physicalBankCount);
    }

    panic_if(physicalBankCount >
                 std::numeric_limits<std::size_t>::max() / routeLayers ||
             ingressCount >
                 std::numeric_limits<std::size_t>::max() /
                     (physicalBankCount * routeLayers),
             "%s route table dimensions overflow size_t", name());

    const std::size_t route_entries =
        ingressCount * physicalBankCount * routeLayers;
    panic_if(routeTable.size() != route_entries,
             "%s expected %zu route entries, got %zu", name(), route_entries,
             routeTable.size());
}

BankAddressMapper::Mapping
BankAddressMapper::map(PacketPtr pkt, std::size_t ingress_id) const
{
    panic_if(ingress_id >= ingressCount,
             "%s received invalid ingress id %zu (count %zu)", name(),
             ingress_id, ingressCount);

    const Addr address = pkt->getAddr();
    const std::uint32_t logical = logicalBankFor(address);
    const std::uint32_t physical = physicalBankFor(address);
    const std::size_t route_start =
        (ingress_id * physicalBankCount + physical) * routeLayers;

    std::vector<std::uint32_t> route(routeLayers);
    std::copy_n(routeTable.begin() + route_start, routeLayers, route.begin());
    return {logical, physical, std::move(route)};
}

std::uint32_t
BankAddressMapper::physicalBankFor(Addr address) const
{
    const std::uint32_t logical = logicalBankFor(address);
    const Addr logical_offset = (address - baseAddr) % logicalBankSize;
    const std::size_t stripe =
        (logical_offset / interleaveSize) % physicalBanksPerLogical;
    return logicalToPhysical[logical * physicalBanksPerLogical + stripe];
}

std::uint32_t
BankAddressMapper::logicalBankFor(Addr address) const
{
    panic_if(address < baseAddr,
             "%s cannot map address %#llx below base address %#llx", name(),
             static_cast<unsigned long long>(address),
             static_cast<unsigned long long>(baseAddr));

    const Addr offset = address - baseAddr;
    return static_cast<std::uint32_t>(
        (offset / logicalBankSize) % logicalBankCount);
}

} // namespace gem5::customxbar
