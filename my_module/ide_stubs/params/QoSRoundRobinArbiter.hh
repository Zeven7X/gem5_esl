#pragma once

#include <cstddef>
#include <cstdint>

#include "base/types.hh"
#include "params/SimObject.hh"

namespace gem5
{

// CMake/CLion-only stand-in. SCons generates the real version from
// my_module/QoSRoundRobinArbiter.py during a gem5 build.
struct QoSRoundRobinArbiterParams : public SimObjectParams
{
    std::size_t num_inputs = 1;
    std::size_t num_outputs = 1;
    std::size_t layer_id = 0;
    std::uint32_t global_output_base = 0;
    std::size_t input_buffer_depth = 4;
    std::size_t output_buffer_depth = 4;
    std::size_t response_buffer_depth = 4;
    Tick arbitration_latency = 0;
    Tick transfer_latency = 0;
};

} // namespace gem5
