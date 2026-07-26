from m5.params import *
from m5.SimObject import SimObject


class QoSRoundRobinArbiter(SimObject):
    type = "QoSRoundRobinArbiter"
    cxx_header = "gem5_xbar/qos_rr_arbiter.hh"
    cxx_class = "gem5::customxbar::QoSRoundRobinArbiter"

    in_ports = VectorResponsePort("Manually connected request inputs")
    out_ports = VectorRequestPort("Manually connected request outputs")
    num_inputs = Param.Unsigned(1, "Number of input ports")
    num_outputs = Param.Unsigned(1, "Number of output ports")
    layer_id = Param.Unsigned(0, "Index into each packet route plan")
    global_output_base = Param.Unsigned(0, "Global id of local output zero")
    input_buffer_depth = Param.Unsigned(4, "Per-input request queue depth")
    output_buffer_depth = Param.Unsigned(4, "Per-output request queue depth")
    response_buffer_depth = Param.Unsigned(4, "Per-input response queue depth")
    arbitration_latency = Param.Latency("0t", "QoS/RR arbitration delay")
    transfer_latency = Param.Latency("0t", "Output transfer delay")
