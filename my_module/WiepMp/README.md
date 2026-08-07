# WiepMp

`WiepMp` implements the process synchronization and generic shared-memory
channel described by `WiepMp_Codex_Design_Spec.md`.

Target environment:

- Linux / CentOS 7
- GCC 7.3 or compatible GCC up to 13.1
- C++14
- POSIX shared memory
- process-shared pthread mutex/condition variable for the control plane
- GCC `__atomic` acquire/release SPSC rings for the data plane

The existing `WiepTlmFifo`, `WiepAxiMstPort`, and `WiepAxiSlvPort` headers are
not modified. AXI and `PacketPtr` encoding belong in a separate Port adapter.

## Build

```bash
cmake -S my_module/WiepMp -B build/WiepMp
cmake --build build/WiepMp -j
```

## Manual process launch

The framework never forks Worker processes. Start every process explicitly:

```bash
./build/WiepMp/wiep_mp_example 0 2 example_session &
./build/WiepMp/wiep_mp_example 1 2 example_session
wait
```

Additional processes can participate in the same barrier without connecting
to Channel 100:

```bash
./build/WiepMp/wiep_mp_example 0 3 three_processes &
./build/WiepMp/wiep_mp_example 1 3 three_processes &
./build/WiepMp/wiep_mp_example 2 3 three_processes
wait
```

Process 0 creates the control area and coordinates startup and Epoch barriers.
The lower endpoint process creates each Channel shared-memory object. Other
processes wait up to the configured timeout and attach to it.

## Integration order

At the end of every Epoch, top-level simulation code calls:

```cpp
simulate(syncPeriod);
manager.synchronize(epoch, curTick());
```

After `synchronize()` returns, Port adapters should check their incoming
`WiepSharedChannel` before running business events for the new Epoch. The
Channel itself does not include gem5 Event, AXI, or Packet logic.
