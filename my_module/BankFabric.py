"""gem5 configuration helper that instantiates and wires a bank fabric."""

from m5.objects.BankAddressMapper import BankAddressMapper
from m5.objects.BankTopology import (
    compile_topology,
    data_widths,
    flattened_logical_mapping,
)
from m5.objects.PhysicalBank import PhysicalBank
from m5.objects.QoSRoundRobinArbiter import QoSRoundRobinArbiter
from m5.objects.SubSystem import SubSystem
from m5.objects.XBarIngress import XBarIngress


class BankFabric(SubSystem):
    """A generated hierarchy of ingresses, XBAR layers, and physical banks."""

    def __init__(self, spec, backend_ports=None, **kwargs):
        super().__init__(**kwargs)
        compiled = compile_topology(spec)
        widths = data_widths(spec)
        layers = tuple(spec.layers)

        self.mapper = BankAddressMapper(
            base_addr=spec.base_addr,
            logical_bank_size=spec.logical_bank_size,
            logical_bank_count=len(spec.logical_to_physical),
            physical_banks_per_logical=len(spec.logical_to_physical[0]),
            interleave_size=spec.interleave_size,
            physical_bank_count=spec.physical_bank_count,
            ingress_count=spec.ingress_count,
            route_layers=len(layers),
            logical_to_physical=flattened_logical_mapping(spec),
            route_table=compiled.flattened_route_table(),
        )

        self.ingresses = [
            XBarIngress(
                mapper=self.mapper,
                ingress_id=ingress,
                ostd_limit=spec.ingress_ostd_limit,
                buffer_depth=spec.ingress_buffer_depth,
                forward_latency=spec.ingress_forward_latency,
            )
            for ingress in range(spec.ingress_count)
        ]

        self.arbiters = []
        layer_arbiter_offsets = []
        for layer_index, layer in enumerate(layers):
            layer_arbiter_offsets.append(len(self.arbiters))
            for node in range(layer.arbiter_count):
                self.arbiters.append(
                    QoSRoundRobinArbiter(
                        num_inputs=layer.inputs_per_arbiter,
                        num_outputs=layer.outputs_per_arbiter,
                        layer_id=layer_index,
                        global_output_base=(
                            compiled.layer_output_bases[layer_index]
                            + node * layer.outputs_per_arbiter
                        ),
                        input_buffer_depth=layer.input_buffer_depth,
                        output_buffer_depth=layer.output_buffer_depth,
                        response_buffer_depth=layer.response_buffer_depth,
                        arbitration_latency=layer.arbitration_latency,
                        transfer_latency=layer.transfer_latency,
                    )
                )

        self.banks = [
            PhysicalBank(
                bank_id=bank,
                data_width_bits=widths[bank],
                request_buffer_depth=spec.bank_request_buffer_depth,
                response_buffer_depth=spec.bank_response_buffer_depth,
                base_latency=spec.bank_base_latency,
                per_beat_latency=spec.bank_per_beat_latency,
            )
            for bank in range(spec.physical_bank_count)
        ]

        def arbiter(layer_index, node):
            return self.arbiters[layer_arbiter_offsets[layer_index] + node]

        for ingress, node, input_port in compiled.ingress_links:
            self.ingresses[ingress].downstream = arbiter(
                0, node
            ).in_ports[input_port]

        for (
            source_layer,
            source_node,
            source_output,
            destination_layer,
            destination_node,
            destination_input,
        ) in compiled.inter_layer_links:
            arbiter(source_layer, source_node).out_ports[
                source_output
            ] = arbiter(destination_layer, destination_node).in_ports[
                destination_input
            ]

        for layer_index, node, output, bank in compiled.bank_links:
            arbiter(layer_index, node).out_ports[output] = self.banks[
                bank
            ].upstream

        if backend_ports is not None:
            self.connect_backends(backend_ports)

    def upstream_port(self, ingress=0):
        return self.ingresses[ingress].upstream

    def downstream_port(self, physical_bank):
        return self.banks[physical_bank].downstream

    def connect_backends(self, backend_ports):
        if len(backend_ports) != len(self.banks):
            raise ValueError(
                "one backend port is required for each physical bank"
            )
        for bank, backend_port in zip(self.banks, backend_ports):
            bank.downstream = backend_port
