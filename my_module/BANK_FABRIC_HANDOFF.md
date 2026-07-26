# Programmable Bank Fabric: Model Handoff

This document is the implementation handoff for the custom gem5 bank fabric.
It is intended to give another coding model enough context to modify the
feature without first reverse-engineering the full repository.

## Scope

The implementation models:

- logical-bank address regions;
- address striping onto physical banks;
- address-dependent routing through a layered XBAR;
- QoS-first, round-robin arbitration at every XBAR node;
- configurable physical-bank data width and beat-dependent service latency;
- request and response backpressure;
- automatic SimObject creation and port wiring from one Python specification.

`PhysicalBank` is currently a timing and bandwidth front end. It does not own
storage. Each bank's downstream port must connect to an existing gem5 memory
controller or memory object.

## Request Path

```text
upstream RequestPort
    -> XBarIngress[ingress_id]
    -> BankAddressMapper
    -> RoutePlan(logical bank, physical bank, per-layer outputs)
    -> QoSRoundRobinArbiter layer 0
    -> ...
    -> QoSRoundRobinArbiter layer N-1
    -> PhysicalBank[physical_bank_id]
    -> downstream memory backend
```

Responses follow the reverse port path. Every arbiter records the original
input port for each accepted timing request in `returnRoutes`.

## Source Map

- `BankAddressMapper.py`
  - gem5 parameters for logical/physical mapping and the compiled route table.
- `include/gem5_xbar/bank_address_mapper.hh`
- `src/gem5_xbar/bank_address_mapper.cc`
  - runtime address mapping and route lookup.
- `PhysicalBank.py`
  - bank id, data width, queue depth, and latency parameters.
- `include/gem5_xbar/physical_bank.hh`
- `src/gem5_xbar/physical_bank.cc`
  - timing request serialization, beat calculation, forwarding, and retries.
- `BankTopology.py`
  - pure-Python dataclasses, validation, address-reference function, and
    layered topology compiler.
- `BankFabric.py`
  - Python `SubSystem` that instantiates all SimObjects and connects ports.
- `include/gem5_xbar/route_plan.hh`
  - packet extension carrying route outputs plus logical/physical bank ids.
- `XBarIngress.py` and `src/gem5_xbar/xbar_ingress.cc`
  - request admission and per-packet mapper invocation.
- `QoSRoundRobinArbiter.py` and
  `src/gem5_xbar/qos_rr_arbiter.cc`
  - per-output QoS/RR arbitration and response return routing.
- `tests/test_bank_topology.py`
  - host-side tests that do not require a built gem5 binary.

## Configuration API

The normal entry point is:

```python
from m5.objects.BankFabric import BankFabric
from m5.objects.BankTopology import BankFabricSpec, LayerSpec

spec = BankFabricSpec(
    ingress_count=4,
    physical_bank_count=8,
    layers=(
        LayerSpec(
            arbiter_count=1,
            inputs_per_arbiter=4,
            outputs_per_arbiter=2,
        ),
        LayerSpec(
            arbiter_count=2,
            inputs_per_arbiter=1,
            outputs_per_arbiter=4,
        ),
    ),
    logical_bank_size=1 << 30,
    logical_to_physical=(
        (0, 1, 2, 3),
        (4, 5, 6, 7),
    ),
    interleave_size=64,
    data_width_bits=256,
    bank_base_latency="20ns",
    bank_per_beat_latency="2ns",
)

system.bank_fabric = BankFabric(spec)
source.port = system.bank_fabric.upstream_port(0)

# Create one backend per physical bank, attach those objects to `system`, then:
system.bank_fabric.connect_backends(
    [backend.port for backend in system.bank_backends]
)
```

`data_width_bits` can be one integer applied to every bank or a sequence with
one entry per physical bank:

```python
data_width_bits=(32, 32, 256, 256, 1024, 1024, 256, 32)
```

## Address Mapping

For packet address `A`:

```text
offset = A - base_addr
logical = (offset / logical_bank_size) % logical_bank_count
logical_offset = offset % logical_bank_size
stripe = (logical_offset / interleave_size)
         % physical_banks_per_logical
physical = logical_to_physical[logical][stripe]
```

All `logical_to_physical` rows must have equal length. Physical bank ids must
be in `[0, physical_bank_count)`. Logical address regions repeat after
`logical_bank_count * logical_bank_size`.

The original packet address is not rewritten. The selected downstream backend
must accept that address.

## Topology Compilation

Every `LayerSpec` describes a homogeneous layer. Port connection is
deterministic:

1. Ingresses connect in order to all input ports of layer 0.
2. Flattened outputs of layer `L` connect in order to flattened inputs of
   layer `L + 1`.
3. Flattened outputs of the last layer connect in order to physical banks.

The compiler requires:

```text
ingress_count
    == layer[0].arbiter_count * layer[0].inputs_per_arbiter

layer[L].arbiter_count * layer[L].outputs_per_arbiter
    == layer[L+1].arbiter_count * layer[L+1].inputs_per_arbiter

physical_bank_count
    == last_layer.arbiter_count * last_layer.outputs_per_arbiter
```

It also verifies that every ingress can reach every physical bank. This
second check catches structurally connected networks that are partitioned
into unreachable subtrees.

Global output ids are allocated consecutively by layer and then by arbiter.
For each `(ingress_id, physical_bank_id)` pair, the compiler finds one
deterministic path and emits one global output id per layer. The flattened
table layout consumed by C++ is:

```text
route_table[ingress][physical_bank][layer]
```

## Physical Bank Timing

The physical bank does not change packet payload size. Its width controls the
number of modeled transfer beats:

```text
beat_bytes = data_width_bits / 8
beats = ceil(max(packet_size, 1) / beat_bytes)
service_latency = base_latency + beats * per_beat_latency
```

Requests are serialized per physical bank. A request pays service latency
before the bank attempts to send it downstream. If the backend rejects it,
the request remains at the queue head and is retried without paying the
service latency again.

The data width must be a positive multiple of 8. Values such as 32, 256, and
1024 bits are valid.

## Timing Protocol Invariants

- `XBarIngress` counts a timing request as outstanding when it accepts it.
  Requests needing a response are released when that response is accepted
  upstream; no-response requests are released when accepted downstream.
- Every arbiter records response-needing timing requests by packet pointer and
  output port. The downstream response must use the same `PacketPtr`, as
  normal gem5 memory objects do.
- Full request queues return `false` and later call `sendRetryReq()`.
- Full response queues return `false` and later call `sendRetryResp()`.
- `PhysicalBank` checks the `RoutePlan` physical id when a mapped packet
  arrives and panics on a misroute.
- Atomic and functional requests use the same address mapping and route but do
  not occupy timing queues.

## Known Limitations

- There is no internal storage in `PhysicalBank`.
- There are no bank statistics yet (beats, queue occupancy, conflicts, or
  utilization).
- There is no row-buffer, open-page, refresh, or read/write turnaround model.
- The topology compiler only connects adjacent homogeneous layers.
- Connections are generated by flattened port order; custom sparse wiring is
  not represented by `BankFabricSpec`.
- The compiler selects one valid path when a graph offers multiple paths; it
  does not load-balance between equivalent paths.
- Snoop traffic is not implemented. The fabric is intended for non-coherent
  request paths.

## Safe Extension Points

- Add an address policy enum to `BankAddressMapper` for XOR/hash mapping.
- Add per-logical-bank pool widths by replacing the rectangular flattened
  mapping with offsets plus entries.
- Add bank statistics in `PhysicalBank`.
- Add row state and a scheduler before `processRequest()`.
- Add explicit link descriptors to `BankTopology.py` while preserving the
  compiled route-table contract.
- Add multiple route candidates per ingress/bank if adaptive routing becomes
  necessary.

When changing address or route-table layouts, update both
`BankTopology.py` and `BankAddressMapper` together. They intentionally mirror
the same indexing contract.

## Validation

Run the host-side topology tests from the repository root:

```bash
python -m unittest my_module.tests.test_bank_topology
```

The real gem5 integration still needs a Linux SCons build:

```bash
scons build/ALL/gem5.opt
```

The `my_module/ide_stubs/params` headers are only for CLion/CMake indexing.
SCons generates the real parameter headers from the Python SimObject files.
