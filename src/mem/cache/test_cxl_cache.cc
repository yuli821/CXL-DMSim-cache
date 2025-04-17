#include "mem/cxl_cache_bridge.hh"
#include "mem/packet.hh"
#include "sim/sim_object.hh"
#include "base/trace.hh"
#include "debug/CXLBridge.hh"
#include <cassert>
#include <iostream>

using namespace gem5;

class DummyPort : public Port
{
  public:
    DummyPort(const std::string &name, SimObject *owner) : Port(name, owner) {}

    AddrRangeList getAddrRanges() const override { return {}; }

    void sendReqRetry() override {}
    void sendRespRetry() override {}
    void sendTimingReq(PacketPtr pkt) { std::cout << "[DummyPort] Sent TimingReq: " << pkt->cmd.toString() << "\n"; }
    void sendTimingResp(PacketPtr pkt) { std::cout << "[DummyPort] Sent TimingResp: " << pkt->cmd.toString() << "\n"; }
};

class DummyBridgeWrapper : public CXLCacheBridge
{
  public:
    DummyBridgeWrapper(const CXLCacheBridgeParams &params) : CXLCacheBridge(params) {}

    void testTranslation() {
        auto read_pkt = new Packet(MemCmd::ReadReq, 0x1000);
        translateToDevice(read_pkt);
        assert(read_pkt->cmd == MemCmd::ReadSharedReq);

        auto write_pkt = new Packet(MemCmd::WriteReq, 0x2000);
        translateToDevice(write_pkt);
        assert(write_pkt->cmd == MemCmd::ReadUniqueReq);

        auto wb_pkt = new Packet(MemCmd::WritebackDirty, 0x3000);
        translateToHost(wb_pkt);
        assert(wb_pkt->cmd == MemCmd::WriteResp);

        auto clean_pkt = new Packet(MemCmd::CleanEvict, 0x4000);
        translateToHost(clean_pkt);
        assert(clean_pkt->cmd == MemCmd::WriteCleanResp);

        std::cout << "All translation tests passed.\n";
    }
};

int main()
{
    CXLCacheBridgeParams dummyParams;
    DummyBridgeWrapper bridge(dummyParams);

    bridge.testTranslation();

    return 0;
}
