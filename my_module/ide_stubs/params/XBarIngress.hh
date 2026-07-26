#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "base/types.hh"
#include "params/SimObject.hh"

namespace gem5
{

namespace customxbar
{
class BankAddressMapper;
}

// CMake/CLion-only stand-in. SCons generates the real version from
// my_module/XBarIngress.py during a gem5 build.
struct XBarIngressParams : public SimObjectParams
{
    std::vector<std::uint32_t> route_outputs;
    std::size_t ostd_limit = 16;
    std::size_t buffer_depth = 16;
    Tick forward_latency = 0;
    customxbar::BankAddressMapper *mapper = nullptr;
    std::size_t ingress_id = 0;
};

} // namespace gem5
