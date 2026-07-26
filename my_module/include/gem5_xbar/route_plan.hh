#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "base/extensible.hh"
#include "mem/packet.hh"

namespace gem5::customxbar
{

/**
 * Per-packet route selected once at the ingress. Each element is a global
 * egress id for the matching arbiter layer.
 */
class RoutePlan : public Extension<Packet, RoutePlan>
{
  public:
    static constexpr std::uint32_t UnmappedBank =
        std::numeric_limits<std::uint32_t>::max();

    explicit RoutePlan(
        std::vector<std::uint32_t> global_outputs,
        std::uint32_t logical_bank = UnmappedBank,
        std::uint32_t physical_bank = UnmappedBank)
        : globalOutputs(std::move(global_outputs)),
          logicalBank(logical_bank),
          physicalBank(physical_bank)
    {
    }

    std::uint32_t outputForLayer(std::size_t layer) const
    {
        if (layer >= globalOutputs.size()) {
            throw std::out_of_range("route plan has no entry for this layer");
        }

        return globalOutputs[layer];
    }

    bool hasBankMapping() const
    {
        return logicalBank != UnmappedBank && physicalBank != UnmappedBank;
    }

    std::uint32_t logicalBankId() const { return logicalBank; }
    std::uint32_t physicalBankId() const { return physicalBank; }

    std::unique_ptr<ExtensionBase> clone() const override
    {
        return std::make_unique<RoutePlan>(*this);
    }

  private:
    std::vector<std::uint32_t> globalOutputs;
    std::uint32_t logicalBank;
    std::uint32_t physicalBank;
};

} // namespace gem5::customxbar
