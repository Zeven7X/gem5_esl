#pragma once

#include <string>

namespace gem5
{

class SimObject;

// CMake/CLion-only stand-in. SCons generates the real version per build.
struct SimObjectParams
{
    std::string name;

    virtual ~SimObjectParams() = default;
    virtual SimObject *create() const { return nullptr; }
};

} // namespace gem5
