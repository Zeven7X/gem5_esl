# WIEP Multi-Process Sync FIFO

This directory contains a Linux/GCC-oriented, process-level synchronization
framework. It is independent of gem5's `BaseXBar` and does not modify the
existing WIEP headers.

## Timing model

For interval `N`, every endpoint performs exactly these operations:

1. `beginInterval(N)` imports slot `N`.
2. The local model runs and calls `nbWrite()` / `nbRead()`.
3. `endInterval(N)` exports all operations once into slot `N + 1`.
4. Main waits until every Worker completes the interval.

Data and read credits therefore become visible one synchronization interval
later. Two ping-pong slots ensure no process reads a slot while its peer writes
that same slot.

## FIFO API

`MpFifo<T, DEPTH, MAX_BURST>` follows the existing WIEP naming style:

```cpp
fifo.nbWrite(message);
fifo.nbRead(message);
auto &message = fifo.nbGet();
fifo.delTrf();
fifo.canPop();
fifo.size();
fifo.emptyNum();
```

`T` must be trivially copyable and cannot be a pointer. A raw `PacketPtr` must
not cross the process boundary. A WIEP adapter should extract the required
`Packet/wiepReq` fields
into a wire structure and reconstruct a process-local pooled Packet on the
receiving side.

## Linux build and test

```bash
cmake -S my_module/mp_sync -B build/mp_sync
cmake --build build/mp_sync -j
ctest --test-dir build/mp_sync --output-on-failure
```

The test starts `worker_sim` as a separate process, sends a request Main to
Worker, returns a response Worker to Main, and checks the delayed read credit.
