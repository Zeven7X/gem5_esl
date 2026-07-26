import unittest

from my_module.BankTopology import (
    BankFabricSpec,
    LayerSpec,
    compile_topology,
    data_widths,
    physical_bank_for,
)


def make_spec(**overrides):
    values = dict(
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
        logical_bank_size=256,
        logical_to_physical=((0, 1, 2, 3), (4, 5, 6, 7)),
        interleave_size=64,
        data_width_bits=256,
    )
    values.update(overrides)
    return BankFabricSpec(**values)


class BankTopologyTest(unittest.TestCase):
    def test_compiles_expected_tree_routes(self):
        compiled = compile_topology(make_spec())

        self.assertEqual(compiled.layer_output_bases, (0, 2))
        self.assertEqual(compiled.routes[0][0], (0, 2))
        self.assertEqual(compiled.routes[0][3], (0, 5))
        self.assertEqual(compiled.routes[0][4], (1, 6))
        self.assertEqual(compiled.routes[0][7], (1, 9))
        self.assertEqual(len(compiled.flattened_route_table()), 4 * 8 * 2)

    def test_address_interleaves_within_each_logical_bank(self):
        spec = make_spec()

        self.assertEqual(physical_bank_for(spec, 0), 0)
        self.assertEqual(physical_bank_for(spec, 64), 1)
        self.assertEqual(physical_bank_for(spec, 192), 3)
        self.assertEqual(physical_bank_for(spec, 256), 4)
        self.assertEqual(physical_bank_for(spec, 320), 5)
        self.assertEqual(physical_bank_for(spec, 512), 0)

    def test_expands_global_or_per_bank_data_width(self):
        self.assertEqual(data_widths(make_spec()), (256,) * 8)
        widths = (32, 64, 128, 256, 512, 1024, 256, 32)
        self.assertEqual(
            data_widths(make_spec(data_width_bits=widths)), widths
        )

    def test_rejects_mismatched_adjacent_layer_ports(self):
        spec = make_spec(
            layers=(
                LayerSpec(1, 4, 3),
                LayerSpec(2, 1, 4),
            )
        )
        with self.assertRaisesRegex(ValueError, "has 3 outputs"):
            compile_topology(spec)

    def test_rejects_topology_without_all_ingress_to_bank_routes(self):
        spec = make_spec(
            ingress_count=2,
            physical_bank_count=4,
            layers=(
                LayerSpec(2, 1, 2),
                LayerSpec(2, 2, 2),
            ),
            logical_to_physical=((0, 1), (2, 3)),
        )
        with self.assertRaisesRegex(
            ValueError, "cannot reach physical bank"
        ):
            compile_topology(spec)

    def test_rejects_non_byte_aligned_width(self):
        with self.assertRaisesRegex(ValueError, "multiple of 8"):
            data_widths(make_spec(data_width_bits=33))


if __name__ == "__main__":
    unittest.main()
