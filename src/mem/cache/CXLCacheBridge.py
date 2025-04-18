from m5.params import *
from m5.SimObject import SimObject

class CXLCacheBridge(SimObject):
    type = 'CXLCacheBridge'
    cxx_class = 'gem5::CXLCacheBridge'
    cxx_header = 'mem/cache/CXLCacheBridge.hh'

    cpu_side_port = ResponsePort("Port that receives from host/cache side")
    mem_side_port = RequestPort("Port that sends to CXL-attached device")

    # Optional config params (for future extensions)
    # latency = Param.Latency("Link latency between bridge and device")

sim_objects = ['CXLCacheBridge']