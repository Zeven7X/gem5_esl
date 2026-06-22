# gem5 + CLion CMake Workspace

## Purpose

This CMake setup is only for IDE indexing, navigation, and future local C++
experiments inside CLion.

It is **not** a replacement for gem5's official SCons build system.

## What this workspace is for

- Opening the gem5 source tree cleanly in CLion.
- Improving header resolution, symbol navigation, and type recognition.
- Emitting `compile_commands.json` for CLion tooling.
- Giving you a clean place to add your own code, such as:
  - `my_module/include/`
  - `my_module/src/`
  - `sim/main.cpp`

## What this workspace does not do

- It does not rebuild the full gem5 build logic in CMake.
- It does not try to compile all gem5 sources as one target.
- It does not replace generated outputs normally produced by SCons.
- It does not guarantee every gem5 source file is independently buildable under
  CMake.

## How it works

The top-level `CMakeLists.txt` provides:

- `gem5_headers`
  - An `INTERFACE` target carrying gem5-related include directories for your
    own code.
- `gem5_source_tree`
  - A non-build custom target whose job is to expose gem5 source files to the
    IDE project model.
- `gem5_ide_probe`
  - A tiny IDE-oriented object target that gives CMake/CLion at least one real
    compile unit, helping `compile_commands.json` stay useful even before you
    add your own executable.
- `sim_main`
  - A future-facing executable target that is created automatically if
    `sim/main.cpp` exists.

During CMake configure, the script in
`cmake/GenerateGem5IdeStubs.cmake` creates lightweight placeholder headers in
the build directory for common gem5 generated header families such as:

- `params/*.hh`
- `debug/*.hh`
- `config/*.hh`
- `enums/*.hh`
- some `arch/.../generated` and `arch/.../gdb-xml` headers

These stubs are for IDE parsing only.

The configure step also creates an IDE-only include alias so your own code can
use source-tree includes in the form:

```cpp
#include "gem5/cpu/base.hh"
```

This alias points `gem5/...` at the repository's `src/...` tree. Existing
public headers under `include/gem5/...` remain available as usual.

## Recommended project layout for your own code

```text
gem5_project/
├── src/
├── include/
├── my_module/
│   ├── include/
│   └── src/
├── sim/
│   └── main.cpp
└── CMakeLists.txt
```

## CLion usage

1. Open `/Users/zevenkk/CLionProjects/gem5_project` as a CLion project.
2. Let CLion load the top-level `CMakeLists.txt`.
3. Use a build directory such as `cmake-build-debug`.
4. Wait for CMake configure to finish once so the IDE stub headers are
   generated.
5. After indexing completes, navigate gem5 code normally in CLion.

## Adding your own executable

If you create `sim/main.cpp`, CMake will automatically create the `sim_main`
target.

If you also place sources under `my_module/src/`, they will be added to
`sim_main` automatically.

Your own code can use includes such as:

```cpp
#include "gem5/cpu/base.hh"
#include "cpu/base.hh"
#include <gem5/m5ops.h>
```

If you prefer, you can also replace the auto target with your own explicit
target definition in `CMakeLists.txt`, for example:

```cmake
add_executable(sim_main
    sim/main.cpp
    my_module/src/example.cpp
)

target_link_libraries(sim_main PRIVATE gem5_headers)
target_include_directories(sim_main PRIVATE my_module/include)
```

## Important limitation

If you need to actually build the real gem5 simulator, keep using SCons, for
example:

```bash
scons build/ALL/gem5.opt
```
