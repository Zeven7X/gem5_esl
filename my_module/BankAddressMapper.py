from m5.params import *
from m5.SimObject import SimObject


class BankAddressMapper(SimObject):
    type = "BankAddressMapper"
    cxx_header = "gem5_xbar/bank_address_mapper.hh"
    cxx_class = "gem5::customxbar::BankAddressMapper"

    base_addr = Param.Addr(0, "Base address of the mapped bank region")
    logical_bank_size = Param.Addr(
        1 << 30, "Contiguous address size assigned to one logical bank"
    )
    logical_bank_count = Param.Unsigned(1, "Number of logical banks")
    physical_banks_per_logical = Param.Unsigned(
        1, "Number of interleaved physical-bank slots per logical bank"
    )
    interleave_size = Param.Addr(
        64, "Address stripe size used to select a physical bank"
    )
    physical_bank_count = Param.Unsigned(
        1, "Number of physical-bank endpoints"
    )
    ingress_count = Param.Unsigned(
        1, "Number of independently routed ingress ports"
    )
    route_layers = Param.Unsigned(
        1, "Number of XBAR layers represented by one route"
    )
    logical_to_physical = VectorParam.Unsigned(
        [0], "Flattened logical-bank to physical-bank lookup table"
    )
    route_table = VectorParam.Unsigned(
        [0],
        "Flattened [ingress][physical bank][layer] global-output table",
    )
