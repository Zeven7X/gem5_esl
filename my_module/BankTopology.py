"""Pure-Python specification and compiler for the programmable bank fabric."""

from dataclasses import dataclass
from functools import lru_cache
from typing import Sequence, Tuple, Union


@dataclass(frozen=True)
class LayerSpec:
    """One homogeneous layer of QoS round-robin arbiters."""

    arbiter_count: int
    inputs_per_arbiter: int
    outputs_per_arbiter: int
    input_buffer_depth: int = 4
    output_buffer_depth: int = 4
    response_buffer_depth: int = 4
    arbitration_latency: str = "0t"
    transfer_latency: str = "0t"


@dataclass(frozen=True)
class BankFabricSpec:
    """Declarative description of bank mapping, topology, and bank timing."""

    ingress_count: int
    physical_bank_count: int
    layers: Sequence[LayerSpec]
    logical_bank_size: int
    logical_to_physical: Sequence[Sequence[int]]
    base_addr: int = 0
    interleave_size: int = 64
    data_width_bits: Union[int, Sequence[int]] = 256
    ingress_ostd_limit: int = 16
    ingress_buffer_depth: int = 16
    ingress_forward_latency: str = "0t"
    bank_request_buffer_depth: int = 16
    bank_response_buffer_depth: int = 16
    bank_base_latency: str = "0t"
    bank_per_beat_latency: str = "1t"


@dataclass(frozen=True)
class CompiledTopology:
    """Concrete links and routes generated from a BankFabricSpec."""

    layer_output_bases: Tuple[int, ...]
    ingress_links: Tuple[Tuple[int, int, int], ...]
    inter_layer_links: Tuple[
        Tuple[int, int, int, int, int, int], ...
    ]
    bank_links: Tuple[Tuple[int, int, int, int], ...]
    routes: Tuple[Tuple[Tuple[int, ...], ...], ...]

    def flattened_route_table(self) -> Tuple[int, ...]:
        return tuple(
            output
            for ingress_routes in self.routes
            for bank_route in ingress_routes
            for output in bank_route
        )


def validate_spec(spec: BankFabricSpec) -> None:
    if spec.ingress_count <= 0:
        raise ValueError("ingress_count must be positive")
    if spec.physical_bank_count <= 0:
        raise ValueError("physical_bank_count must be positive")
    if not spec.layers:
        raise ValueError("at least one XBAR layer is required")
    if spec.logical_bank_size <= 0:
        raise ValueError("logical_bank_size must be positive")
    if spec.interleave_size <= 0:
        raise ValueError("interleave_size must be positive")
    if spec.base_addr < 0:
        raise ValueError("base_addr cannot be negative")
    if not spec.logical_to_physical:
        raise ValueError("logical_to_physical cannot be empty")

    row_width = len(spec.logical_to_physical[0])
    if row_width == 0:
        raise ValueError("each logical bank needs a physical-bank pool")
    for logical, row in enumerate(spec.logical_to_physical):
        if len(row) != row_width:
            raise ValueError(
                "all logical_to_physical rows must have equal length"
            )
        for physical in row:
            if physical < 0 or physical >= spec.physical_bank_count:
                raise ValueError(
                    "logical bank {} references invalid physical bank {}".format(
                        logical, physical
                    )
                )

    for layer_index, layer in enumerate(spec.layers):
        if (
            layer.arbiter_count <= 0
            or layer.inputs_per_arbiter <= 0
            or layer.outputs_per_arbiter <= 0
        ):
            raise ValueError(
                "layer {} dimensions must be positive".format(layer_index)
            )
        if (
            layer.input_buffer_depth <= 0
            or layer.output_buffer_depth <= 0
            or layer.response_buffer_depth <= 0
        ):
            raise ValueError(
                "layer {} queue depths must be positive".format(layer_index)
            )

    if spec.ingress_ostd_limit <= 0 or spec.ingress_buffer_depth <= 0:
        raise ValueError("ingress limits must be positive")
    if (
        spec.bank_request_buffer_depth <= 0
        or spec.bank_response_buffer_depth <= 0
    ):
        raise ValueError("physical-bank queue depths must be positive")

    data_widths(spec)


def data_widths(spec: BankFabricSpec) -> Tuple[int, ...]:
    if isinstance(spec.data_width_bits, int):
        widths = (spec.data_width_bits,) * spec.physical_bank_count
    else:
        widths = tuple(spec.data_width_bits)
        if len(widths) != spec.physical_bank_count:
            raise ValueError(
                "data_width_bits needs one value or one value per physical bank"
            )

    for width in widths:
        if width <= 0 or width % 8 != 0:
            raise ValueError(
                "every physical-bank data width must be a positive multiple of 8"
            )
    return widths


def flattened_logical_mapping(spec: BankFabricSpec) -> Tuple[int, ...]:
    return tuple(
        physical
        for logical_pool in spec.logical_to_physical
        for physical in logical_pool
    )


def physical_bank_for(spec: BankFabricSpec, address: int) -> int:
    """Reference implementation matching BankAddressMapper::physicalBankFor."""

    validate_spec(spec)
    if address < spec.base_addr:
        raise ValueError("address is below base_addr")

    offset = address - spec.base_addr
    logical = (offset // spec.logical_bank_size) % len(
        spec.logical_to_physical
    )
    logical_offset = offset % spec.logical_bank_size
    pool = spec.logical_to_physical[logical]
    stripe = (logical_offset // spec.interleave_size) % len(pool)
    return pool[stripe]


def compile_topology(spec: BankFabricSpec) -> CompiledTopology:
    validate_spec(spec)
    layers = tuple(spec.layers)

    layer_output_bases = []
    next_global_output = 0
    for layer in layers:
        layer_output_bases.append(next_global_output)
        next_global_output += (
            layer.arbiter_count * layer.outputs_per_arbiter
        )

    first_inputs = tuple(
        (node, input_port)
        for node in range(layers[0].arbiter_count)
        for input_port in range(layers[0].inputs_per_arbiter)
    )
    if len(first_inputs) != spec.ingress_count:
        raise ValueError(
            "first layer exposes {} inputs but ingress_count is {}".format(
                len(first_inputs), spec.ingress_count
            )
        )
    ingress_links = tuple(
        (ingress, node, input_port)
        for ingress, (node, input_port) in enumerate(first_inputs)
    )

    inter_layer_links = []
    next_nodes_by_output = {}
    for layer_index in range(len(layers) - 1):
        source_layer = layers[layer_index]
        destination_layer = layers[layer_index + 1]
        source_outputs = tuple(
            (node, output)
            for node in range(source_layer.arbiter_count)
            for output in range(source_layer.outputs_per_arbiter)
        )
        destination_inputs = tuple(
            (node, input_port)
            for node in range(destination_layer.arbiter_count)
            for input_port in range(destination_layer.inputs_per_arbiter)
        )
        if len(source_outputs) != len(destination_inputs):
            raise ValueError(
                "layer {} has {} outputs but layer {} has {} inputs".format(
                    layer_index,
                    len(source_outputs),
                    layer_index + 1,
                    len(destination_inputs),
                )
            )

        for (source_node, source_output), (
            destination_node,
            destination_input,
        ) in zip(source_outputs, destination_inputs):
            inter_layer_links.append(
                (
                    layer_index,
                    source_node,
                    source_output,
                    layer_index + 1,
                    destination_node,
                    destination_input,
                )
            )
            next_nodes_by_output[
                (layer_index, source_node, source_output)
            ] = destination_node

    last_index = len(layers) - 1
    last_layer = layers[last_index]
    last_outputs = tuple(
        (node, output)
        for node in range(last_layer.arbiter_count)
        for output in range(last_layer.outputs_per_arbiter)
    )
    if len(last_outputs) != spec.physical_bank_count:
        raise ValueError(
            "last layer exposes {} outputs but physical_bank_count is {}".format(
                len(last_outputs), spec.physical_bank_count
            )
        )

    bank_links = tuple(
        (last_index, node, output, bank)
        for bank, (node, output) in enumerate(last_outputs)
    )
    bank_by_output = {
        (last_index, node, output): bank
        for _, node, output, bank in bank_links
    }

    @lru_cache(maxsize=None)
    def find_route(layer_index: int, node: int, bank: int):
        layer = layers[layer_index]
        for output in range(layer.outputs_per_arbiter):
            global_output = (
                layer_output_bases[layer_index]
                + node * layer.outputs_per_arbiter
                + output
            )
            output_key = (layer_index, node, output)
            if layer_index == last_index:
                if bank_by_output[output_key] == bank:
                    return (global_output,)
                continue

            next_node = next_nodes_by_output[output_key]
            suffix = find_route(layer_index + 1, next_node, bank)
            if suffix is not None:
                return (global_output,) + suffix
        return None

    all_routes = []
    for ingress, root_node, _ in ingress_links:
        ingress_routes = []
        for bank in range(spec.physical_bank_count):
            route = find_route(0, root_node, bank)
            if route is None:
                raise ValueError(
                    "ingress {} cannot reach physical bank {}".format(
                        ingress, bank
                    )
                )
            ingress_routes.append(route)
        all_routes.append(tuple(ingress_routes))

    return CompiledTopology(
        layer_output_bases=tuple(layer_output_bases),
        ingress_links=ingress_links,
        inter_layer_links=tuple(inter_layer_links),
        bank_links=bank_links,
        routes=tuple(all_routes),
    )
