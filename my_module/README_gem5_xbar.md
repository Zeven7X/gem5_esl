# Custom gem5 XBAR Building Blocks

`XBarIngress` and `QoSRoundRobinArbiter` are new gem5 `SimObject` building
blocks. They do not inherit from `BaseXBar`.

## Components

- `XBarIngress`
  - One `ResponsePort` named `upstream` and one `RequestPort` named
    `downstream`.
  - Enforces `ostd_limit` and adds a per-packet route plan at entry.
- `QoSRoundRobinArbiter`
  - `num_inputs` vector `in_ports` and `num_outputs` vector `out_ports`.
  - Each output arbitrates among matching input heads with highest packet QoS,
    then round-robin. The initial RR winner is local input 0.
  - Keeps independent input, output, and response buffers.

## Route Plan

`XBarIngress.route_outputs` contains one global output id per layer. An
arbiter uses `layer_id` to read its entry and converts the global id into a
local port index using `global_output_base`.

For example, an ingress with `route_outputs=[1, 5]` enters a two-layer
network. Layer 0 selects global output 1; layer 1 selects global output 5.

## Manual Python Wiring

```python
ingress = XBarIngress(route_outputs=[1, 5], ostd_limit=8)
layer0 = QoSRoundRobinArbiter(
    num_inputs=4, num_outputs=2, layer_id=0, global_output_base=0
)
layer1 = QoSRoundRobinArbiter(
    num_inputs=2, num_outputs=4, layer_id=1, global_output_base=4
)

ingress.downstream = layer0.in_ports[0]
layer0.out_ports[1] = layer1.in_ports[0]
layer1.out_ports[1] = system.mem_ctrl.port
```

Set `arbitration_latency="0t"` and `transfer_latency="0t"` on successive
nodes to allow events from multiple layers to execute in the same tick. A
non-zero value inserts an event-driven delay before the next node sees the
request.
