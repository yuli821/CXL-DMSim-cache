#include "mem/cache/CXLCacheBridge.hh"
#include "mem/packet.hh"
#include "sim/clocked_object.hh"
#include "params/CXLCacheBridge.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include <iostream>

using namespace gem5;

namespace {

class DummyParams : public CXLCacheBridgeParams {
  public:
    DummyParams() {
        name = "cxl_unit_test";
        clock = 1 * SimClock::Int::ns;
        eventq = &mainEventQueue;
    }
};

void testTranslateToDevice() {
    DummyParams p;
    CXLCacheBridge bridge(p);

    auto pkt = new Packet(MemCmd::ReadReq, 0x1000);
    bridge.translateToDevice(pkt);
    if (pkt->cmd != MemCmd::ReadSharedReq)
        panic("ReadReq not translated to ReadSharedReq");

    pkt = new Packet(MemCmd::WriteReq, 0x2000);
    bridge.translateToDevice(pkt);
    if (pkt->cmd != MemCmd::ReadUniqueReq)
        panic("WriteReq not translated to ReadUniqueReq");

    delete pkt;
}

void testTranslateToHost() {
    DummyParams p;
    CXLCacheBridge bridge(p);

    auto pkt = new Packet(MemCmd::WritebackDirty, 0x3000);
    bridge.translateToHost(pkt);
    if (pkt->cmd != MemCmd::WriteResp)
        panic("WritebackDirty not translated to WriteResp");

    pkt = new Packet(MemCmd::CleanEvict, 0x4000);
    bridge.translateToHost(pkt);
    if (pkt->cmd != MemCmd::WriteCleanResp)
        panic("CleanEvict not translated to WriteCleanResp");

    delete pkt;
}

} // anonymous namespace

int main() {
    testTranslateToDevice();
    testTranslateToHost();
    std::cout << "CXLCacheBridge unit tests passed.\n";
    return 0;
}
