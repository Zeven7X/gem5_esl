from m5.params import *
from m5.SimObject import SimObject


class XBarIngress(SimObject):
    type = "XBarIngress"
    cxx_header = "gem5_xbar/xbar_ingress.hh"
    cxx_class = "gem5::customxbar::XBarIngress"

    upstream = ResponsePort("Request ingress from an upstream RequestPort")
    downstream = RequestPort("Request egress to the first arbiter or memory")
    ostd_limit = Param.Unsigned(16, "Maximum accepted timing requests")
    buffer_depth = Param.Unsigned(16, "Request and response queue depth")
    forward_latency = Param.Latency("0t", "Ingress forwarding latency")
    mapper = Param.BankAddressMapper(
        NULL, "Optional address mapper used to create per-packet routes"
    )
    ingress_id = Param.Unsigned(
        0, "Ingress index used when selecting an address-dependent route"
    )
    route_outputs = VectorParam.Unsigned(
        [], "Fallback global output id selected for every arbiter layer"
    )
