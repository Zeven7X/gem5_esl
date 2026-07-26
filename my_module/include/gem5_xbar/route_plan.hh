#pragma once

#include <cstdint>
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
    explicit RoutePlan(std::vector<std::uint32_t> global_outputs)
        : globalOutputs(std::move(global_outputs))
    {
    }

    std::uint32_t outputForLayer(std::size_t layer) const
    {
        if (layer >= globalOutputs.size()) {
            throw std::out_of_range("route plan has no entry for this layer");
        }

        return globalOutputs[layer];
    }

    std::unique_ptr<ExtensionBase> clone() const override
    {
        return std::make_unique<RoutePlan>(*this);
    }

  private:
    std::vector<std::uint32_t> globalOutputs;
};

} // namespace gem5::customxbar
