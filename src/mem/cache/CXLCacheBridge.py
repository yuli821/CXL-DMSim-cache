from m5.params import *
from m5.SimObject import SimObject

class CXLCacheBridge(SimObject):
    type = 'CXLCacheBridge'
    cxx_class = 'gem5::CXLCacheBridge'
    cxx_header = "mem/CXLCacheBridge.hh"

    cpu_side_port = SlavePort("Port that connects to host coherence fabric")
    mem_side_port = MasterPort("Port that connects to CXL HMC or memory")

sim_objects = ['CXLCacheBridge']