# Copyright (c) 2021 The Regents of the University of California
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


from typing import (
    List,
    Sequence,
)

from m5.objects import (  # I'm adding this to add CPU and Cache
    X86ISA,
    Addr,
    AddrRange,
    BaseXBar,
    Bridge,
    Cache,
    CoherentXBar,
    CowDiskImage,
    CXLBridge,
    CXLMemBar,
    IdeDisk,
    IOXBar,
    L2XBar,
    Pc,
    Port,
    Process,
    RawDiskImage,
    # X86AtomicSimpleCPU,
    X86E820Entry,
    X86FsLinux,
    X86IntelMPBus,
    X86IntelMPBusHierarchy,
    X86IntelMPIOAPIC,
    X86IntelMPIOIntAssignment,
    X86IntelMPProcessor,
    X86SMBiosBiosInformation,
)
from m5.params import Latency
from m5.util.convert import toMemorySize

from gem5.components.boards.abstract_system_board import AbstractSystemBoard
from gem5.components.boards.kernel_disk_workload import KernelDiskWorkload
from gem5.components.boards.se_binary_workload import SEBinaryWorkload
from gem5.components.cachehierarchies.abstract_cache_hierarchy import (
    AbstractCacheHierarchy,
)
from gem5.components.processors.random_generator import RandomGenerator
from gem5.components.cachehierarchies.classic.caches.l1dcache import L1DCache
from gem5.components.cachehierarchies.classic.caches.l1icache import L1ICache
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.memory.abstract_memory_system import AbstractMemorySystem
from gem5.components.processors.abstract_processor import AbstractProcessor
from gem5.isas import ISA
from gem5.resources.resource import AbstractResource
from gem5.utils.override import overrides


class X86Board(AbstractSystemBoard, KernelDiskWorkload):
    """
    A board capable of full system simulation for X86.

    **Limitations**
    * Currently, this board's memory is hardcoded to 3GB.
    * Much of the I/O subsystem is hard coded.
    """

    def __init__(
        self,
        clk_freq: str,
        processor: AbstractProcessor,
        memory: AbstractMemorySystem,
        cache_hierarchy: AbstractCacheHierarchy,
        cxl_memory: AbstractMemorySystem,
        is_asic: bool,
    ) -> None:
        super().__init__(
            clk_freq=clk_freq,
            processor=processor,
            memory=memory,
            cache_hierarchy=cache_hierarchy,
            cxl_memory=cxl_memory,
            is_asic=is_asic,
        )

        if self.get_processor().get_isa() != ISA.X86:
            raise Exception(
                "The X86Board requires a processor using the X86 "
                f"ISA. Current processor ISA: '{processor.get_isa().name}'."
            )

    @overrides(AbstractSystemBoard)
    def _setup_board(self) -> None:
        self.pc = Pc()

        self.workload = X86FsLinux()

        # North Bridge
        self.iobus = IOXBar()

        # Set up all of the I/O.
        self._setup_io_devices()

        self.m5ops_base = 0xFFFF0000

        self._setup_cxl_type2_device()

    def _setup_io_devices(self):
        """Sets up the x86 IO devices.

        .. note::

            This is mostly copy-paste from prior X86 FS setups. Some of it
            may not be documented and there may be bugs.
        """

        # Constants similar to x86_traits.hh
        IO_address_space_base = 0x8000000000000000
        pci_config_address_space_base = 0xC000000000000000
        interrupts_address_space_base = 0xA000000000000000
        APIC_range_size = 1 << 12

        # Setup memory system specific settings.
        if self.get_cache_hierarchy().is_ruby():
            self.pc.attachIO(self.get_io_bus(), [self.pc.south_bridge.ide.dma, self.pc.south_bridge.cxlmemory.dma])
        else:
            # # Constants similar to x86_traits.hh
            IO_address_space_base = 0x8000000000000000
            pci_config_address_space_base = 0xC000000000000000
            interrupts_address_space_base = 0xA000000000000000
            APIC_range_size = 1 << 12

            # Configure CXLBridge
            self.bridge = CXLBridge(bridge_lat="50ns", proto_proc_lat="12ns", req_fifo_depth=128, resp_fifo_depth=128)
            self.bridge.mem_side_port = self.get_io_bus().cpu_side_ports
            self.bridge.cpu_side_port = (
                self.get_cache_hierarchy().get_mem_side_port()
            )

            self.bridge.ranges = [
                AddrRange(0xC0000000, 0xFFFF0000),
                AddrRange(
                    IO_address_space_base, interrupts_address_space_base - 1
                ),
                AddrRange(pci_config_address_space_base, Addr.max),
            ]

            # Configure CXL Device
            cxl_mem_start = 0x100000000
            cxl_dram = self.get_cxl_memory()
            self.cxl_mem_range = AddrRange(Addr(cxl_mem_start), size=cxl_dram.get_size())
            self.bridge.ranges.append(self.cxl_mem_range)
            self.pc.south_bridge.cxlmemory.cxl_mem_range = self.cxl_mem_range
            cxl_dram.set_memory_range([self.cxl_mem_range])
            cxl_abstract_mems = []
            for mc in cxl_dram.get_memory_controllers():
                cxl_abstract_mems.append(mc.dram)
            self.memories.extend(cxl_abstract_mems)
            self.cxl_mem_bus = CXLMemBar()
            self.cxl_mem_bus.cpu_side_ports = self.pc.south_bridge.cxlmemory.mem_req_port
            for _, port in cxl_dram.get_mem_ports():
                self.cxl_mem_bus.mem_side_ports = port

            self.pc.south_bridge.cxlmemory.BAR0.size = cxl_dram.get_size_str()
            if self._is_asic:
                self.pc.south_bridge.cxlmemory.proto_proc_lat = Latency("15ns")
                self.pc.south_bridge.cxlmemory.rsp_size = 48
                self.pc.south_bridge.cxlmemory.req_size = 48
            else:
                self.pc.south_bridge.cxlmemory.proto_proc_lat = Latency("60ns")
                self.pc.south_bridge.cxlmemory.rsp_size = 36
                self.pc.south_bridge.cxlmemory.req_size = 36

            self.apicbridge = Bridge(delay="50ns")
            self.apicbridge.cpu_side_port = self.get_io_bus().mem_side_ports
            self.apicbridge.mem_side_port = (
                self.get_cache_hierarchy().get_cpu_side_port()
            )
            self.apicbridge.ranges = [
                AddrRange(
                    interrupts_address_space_base,
                    interrupts_address_space_base
                    + self.get_processor().get_num_cores() * APIC_range_size
                    - 1,
                )
            ]
            self.pc.attachIO(self.get_io_bus())

        # Add in a Bios information structure.
        self.workload.smbios_table.structures = [X86SMBiosBiosInformation()]

        # Set up the Intel MP table
        base_entries = []
        ext_entries = []
        for i in range(self.get_processor().get_num_cores()):
            bp = X86IntelMPProcessor(
                local_apic_id=i,
                local_apic_version=0x14,
                enable=True,
                bootstrap=(i == 0),
            )
            base_entries.append(bp)
        io_apic = X86IntelMPIOAPIC(
            id=self.get_processor().get_num_cores(),
            version=0x11,
            enable=True,
            address=0xFEC00000,
        )

        self.pc.south_bridge.io_apic.apic_id = io_apic.id
        base_entries.append(io_apic)
        pci_bus = X86IntelMPBus(bus_id=0, bus_type="PCI   ")
        base_entries.append(pci_bus)
        isa_bus = X86IntelMPBus(bus_id=1, bus_type="ISA   ")
        base_entries.append(isa_bus)
        connect_busses = X86IntelMPBusHierarchy(
            bus_id=1, subtractive_decode=True, parent_bus=0
        )
        ext_entries.append(connect_busses)

        pci_dev4_inta = X86IntelMPIOIntAssignment(
            interrupt_type="INT",
            polarity="ConformPolarity",
            trigger="ConformTrigger",
            source_bus_id=0,
            source_bus_irq=0 + (4 << 2),
            dest_io_apic_id=io_apic.id,
            dest_io_apic_intin=16,
        )

        base_entries.append(pci_dev4_inta)

        def assignISAInt(irq, apicPin):
            assign_8259_to_apic = X86IntelMPIOIntAssignment(
                interrupt_type="ExtInt",
                polarity="ConformPolarity",
                trigger="ConformTrigger",
                source_bus_id=1,
                source_bus_irq=irq,
                dest_io_apic_id=io_apic.id,
                dest_io_apic_intin=0,
            )
            base_entries.append(assign_8259_to_apic)

            assign_to_apic = X86IntelMPIOIntAssignment(
                interrupt_type="INT",
                polarity="ConformPolarity",
                trigger="ConformTrigger",
                source_bus_id=1,
                source_bus_irq=irq,
                dest_io_apic_id=io_apic.id,
                dest_io_apic_intin=apicPin,
            )
            base_entries.append(assign_to_apic)

        assignISAInt(0, 2)
        assignISAInt(1, 1)

        for i in range(3, 15):
            assignISAInt(i, i)

        self.workload.intel_mp_table.base_entries = base_entries
        self.workload.intel_mp_table.ext_entries = ext_entries

        entries = [
            # Mark the first megabyte of memory as reserved
            X86E820Entry(addr=0, size="639kB", range_type=1),
            X86E820Entry(addr=0x9FC00, size="385kB", range_type=2),
            # Mark the rest of physical memory as available
            X86E820Entry(
                addr=0x100000,
                size=f"{self.mem_ranges[0].size() - 0x100000:d}B",
                range_type=1,
            ),
        ]

        # Reserve the last 16kB of the 32-bit address space for m5ops
        entries.append(
            X86E820Entry(addr=0xFFFF0000, size="64kB", range_type=2)
        )

        entries.append(X86E820Entry(addr=0x100000000, size=f"{self.cxl_mem_range.size()}B", range_type=1))

        self.workload.e820_table.entries = entries

    def _setup_cxl_type2_device(self):
        # setup an core
        hmc_addrRangeList = [ctrl.dram.range for ctrl in self.get_memory().get_memory_controllers()]
        dmc_addrRangeList = [ctrl.dram.range for ctrl in self.get_cxl_memory().get_memory_controllers()]
        self.afu_host = RandomGenerator(
            duration='5s',
            min_addr=0x0,
            max_addr=self.get_memory().get_size(),
            block_size=64,
            min_period='10ns',
            max_period='10ns',
            read_percent=60  # 60% reads, 40% writes
        )
        self.afu_device = RandomGenerator(
            duration='5s',
            min_addr=0x100000000,
            max_addr=0x100000000 + self.get_cxl_memory().get_size(),
            block_size=64,
            min_period='10ns',
            max_period='10ns',
            read_percent=60  # 60% reads, 40% writes
        )
        self.afu_hmc=Cache(
            assoc=16,
            tag_latency=10,
            data_latency=10,
            response_latency=1,
            mshrs=20,
            size="2MB",
            tgts_per_mshr=12,
            write_buffers=32,
            writeback_clean=False,
            clusivity="mostly_excl",
            addr_ranges=hmc_addrRangeList)
        self.afu_dmc=Cache(
            assoc=16,
            tag_latency=50,
            data_latency=50,
            response_latency=50,
            mshrs=32,
            size="2MB",
            tgts_per_mshr=12,
            write_buffers=32,
            writeback_clean=False,
            clusivity="mostly_excl",
            addr_ranges=dmc_addrRangeList,)
        #connection
        self.afu_hmc.cpu_side = self.afu_host.port
        self.afu_dmc.cpu_side = self.afu_device.port
        self.afu_hmc.mem_side = self.cache_hierarchy.membus.cpu_side_ports
        self.afu_dmc.mem_side = self.cxl_mem_bus.cpu_side_ports

    @overrides(AbstractSystemBoard)
    def has_io_bus(self) -> bool:
        return True

    @overrides(AbstractSystemBoard)
    def get_io_bus(self) -> BaseXBar:
        return self.iobus

    @overrides(AbstractSystemBoard)
    def has_dma_ports(self) -> bool:
        return True

    @overrides(AbstractSystemBoard)
    def get_dma_ports(self) -> Sequence[Port]:
        return [self.pc.south_bridge.ide.dma, self.iobus.mem_side_ports, self.pc.south_bridge.cxlmemory.dma]

    @overrides(AbstractSystemBoard)
    def has_coherent_io(self) -> bool:
        return True

    @overrides(AbstractSystemBoard)
    def get_mem_side_coherent_io_port(self) -> Port:
        return self.iobus.mem_side_ports

    @overrides(AbstractSystemBoard)
    def _setup_memory_ranges(self):
        memory = self.get_memory()

        if memory.get_size() > toMemorySize("3GB"):
            raise Exception(
                "X86Board currently only supports memory sizes up "
                "to 3GB because of the I/O hole."
            )
        data_range = AddrRange(memory.get_size())
        memory.set_memory_range([data_range])
        cpu_abstract_mems = []
        for mc in memory.get_memory_controllers():
            cpu_abstract_mems.append(mc.dram)
        self.memories = cpu_abstract_mems
        # Add the address range for the IO
        self.mem_ranges = [
            data_range,  # All data
            AddrRange(0xC0000000, size=0x100000),  # For I/0
        ]

    @overrides(KernelDiskWorkload)
    def get_disk_device(self):
        return "/dev/hda1"

    @overrides(KernelDiskWorkload)
    def _add_disk_to_board(self, disk_image: AbstractResource):
        ide_disk = IdeDisk()
        ide_disk.driveID = "device0"
        ide_disk.image = CowDiskImage(
            child=RawDiskImage(read_only=True), read_only=False
        )
        ide_disk.image.child.image_file = disk_image.get_local_path()

        # Attach the SimObject to the system.
        self.pc.south_bridge.ide.disks = [ide_disk]

    @overrides(KernelDiskWorkload)
    def get_default_kernel_args(self) -> List[str]:
        return [
            "earlyprintk=ttyS0",
            "console=ttyS0",
            "lpj=7999923",
            "root={root_value}",
            "disk_device={disk_device}",
        ]

