#pragma once

#include <cstddef>
#include <cstdint>

#include "base/types.hh"
#include "params/SimObject.hh"

namespace gem5
{

// CMake/CLion-only stand-in. SCons generates the real version from
// my_module/PhysicalBank.py during a gem5 build.
struct PhysicalBankParams : public SimObjectParams
{
    std::uint32_t bank_id = 0;
    std::size_t data_width_bits = 256;
    std::size_t request_buffer_depth = 16;
    std::size_t response_buffer_depth = 16;
    Tick base_latency = 0;
    Tick per_beat_latency = 1;
};

} // namespace gem5
