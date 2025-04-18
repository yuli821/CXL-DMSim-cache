#include "mem/cache/CXLCacheBridge.hh"
#include "params/CXLCacheBridge.hh"
#include "sim/clocked_object.hh"
#include "mem/packet.hh"
#include "sim/init.hh"
#include "sim/sim_exit.hh"
#include "base/trace.hh"
#include <iostream>

using namespace gem5;

class DummyParams : public CXLCacheBridgeParams
{
public:
    DummyParams() {
        name = "cxl_test";
        clock = 1 * SimClock::Int::ns;
        eventq = &mainEventQueue;
    }
};

int main()
{
    // Initialize simulation
    gem5::initSimStats();
    gem5::initSimObjects();

    DummyParams p;
    CXLCacheBridge bridge(p);

    // Translate a host packet
    auto pkt = new Packet(MemCmd::ReadReq, 0x1000);
    bridge.translateToDevice(pkt);
    if (pkt->cmd != MemCmd::ReadSharedReq)
        panic("ReadReq should become ReadSharedReq");

    delete pkt;

    auto pkt2 = new Packet(MemCmd::WritebackDirty, 0x2000);
    bridge.translateToHost(pkt2);
    if (pkt2->cmd != MemCmd::WriteResp)
        panic("WritebackDirty should become WriteResp");

    delete pkt2;

    std::cout << "CXLCacheBridge unit test passed.\n";
    exitSimLoop("done", 0);
    return 0;
}
