from m5.params import *
from m5.SimObject import SimObject


class PhysicalBank(SimObject):
    type = "PhysicalBank"
    cxx_header = "gem5_xbar/physical_bank.hh"
    cxx_class = "gem5::customxbar::PhysicalBank"

    upstream = ResponsePort("Request input from the final XBAR layer")
    downstream = RequestPort("Request output to a memory controller")
    bank_id = Param.Unsigned(0, "Physical bank id checked against RoutePlan")
    data_width_bits = Param.Unsigned(
        256, "Number of data bits transferred by one bank beat"
    )
    request_buffer_depth = Param.Unsigned(
        16, "Number of accepted requests waiting for the backend"
    )
    response_buffer_depth = Param.Unsigned(
        16, "Number of backend responses waiting for the XBAR"
    )
    base_latency = Param.Latency("0t", "Fixed service latency per request")
    per_beat_latency = Param.Latency(
        "1t", "Additional service latency for each data-width beat"
    )
