#pragma once

#include <cstdint>

struct DemoRequest
{
    std::uint64_t tag;
    std::uint64_t address;
    std::uint32_t value;
    std::uint8_t command;
};

struct DemoResponse
{
    std::uint64_t tag;
    std::uint32_t value;
    std::uint8_t status;
};
