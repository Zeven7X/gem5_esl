#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "base/types.hh"
#include "params/SimObject.hh"

namespace gem5
{

// CMake/CLion-only stand-in. SCons generates the real version from
// my_module/BankAddressMapper.py during a gem5 build.
struct BankAddressMapperParams : public SimObjectParams
{
    Addr base_addr = 0;
    Addr logical_bank_size = 1ULL << 30;
    std::size_t logical_bank_count = 1;
    std::size_t physical_banks_per_logical = 1;
    Addr interleave_size = 64;
    std::size_t physical_bank_count = 1;
    std::size_t ingress_count = 1;
    std::size_t route_layers = 1;
    std::vector<std::uint32_t> logical_to_physical = {0};
    std::vector<std::uint32_t> route_table = {0};
};

} // namespace gem5
