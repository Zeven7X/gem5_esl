# XBAR Logic Model Demo

This is a standalone C++ cycle-level XBAR logic model placed under the gem5
source tree for CLion experiments.

It does not reuse gem5's existing XBar objects. The model is intentionally kept
as new C++ code so it can be run directly from `sim/main.cpp`, and later wrapped
as a gem5 `SimObject` if needed.

## Files

- `my_module/include/xbar_logic_model.hh`
- `my_module/src/xbar_logic_model.cpp`
- `sim/main.cpp`

## Modeled Structure

- 16 input ports.
- 16 logical banks.
- 2 physical sub-banks per logical bank.
- 32 physical sub-banks total.
- Fixed middle interleave network:
  - stage 1: 4x4
  - stage 2: 2x2
  - stage 3: 2x2
- Read and write paths arbitrate independently in the middle network.
- Read and write merge only at the physical sub-bank side.
- Middle network latency is modeled as 1 cycle.

## Address Mapping

```text
addr[5:0]   -> offset inside bank, 16 words
addr[9:6]   -> logical bank id, 0..15
addr[10]    -> physical sub-bank inside the logical bank
```

The global physical sub-bank index is:

```text
physical_sub_bank = logical_bank * 2 + sub_bank
```

## Backpressure Rules

- If a request loses arbitration inside the middle XBAR network, it remains at
  the head of the input port queue.
- If a request has passed the XBAR but loses bank-side arbitration, it remains
  in the post-XBAR pending queue.
- Each physical sub-bank can accept at most one request per cycle.
- Read and write cannot enter the same physical sub-bank in the same cycle.

## Arbitration

Middle XBAR stages use QoS + round-robin:

- Higher QoS wins.
- If QoS ties, the stage output's round-robin state selects the winner.

Bank-side arbitration is replaceable through `BankArbitrationPolicy`.

Built-in policies:

- `QoSThenRR`
- `ReadPriority`
- `WritePriority`
- `SevenReadsOneWrite`

## Running

The existing `CMakeLists.txt` automatically creates `sim_main` when
`sim/main.cpp` exists. In CLion, reload CMake and run the `sim_main` target.

You can also compile directly:

```bash
clang++ -std=c++17 -Wall -Wextra \
  -I /Users/zevenkk/CLionProjects/gem5_project/my_module/include \
  /Users/zevenkk/CLionProjects/gem5_project/sim/main.cpp \
  /Users/zevenkk/CLionProjects/gem5_project/my_module/src/*.cpp \
  -o /tmp/gem5_xbar_logic_demo
```

Then run:

```bash
/tmp/gem5_xbar_logic_demo
```
