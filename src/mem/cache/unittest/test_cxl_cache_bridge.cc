#include "base/unittest.hh"
#include "mem/cxl_cache_bridge.hh"
#include "mem/packet.hh"

using namespace gem5;

class DummyParams : public CXLCacheBridgeParams {
  public:
    DummyParams() {
        name = "dummy_bridge";
    }
};

TEST(CXLCacheBridge, TranslateToDevice)
{
    DummyParams params;
    CXLCacheBridge bridge(params);

    auto pkt = new Packet(MemCmd::ReadReq, 0x1000);
    bridge.translateToDevice(pkt);
    EXPECT_EQ(pkt->cmd, MemCmd::ReadSharedReq);

    pkt = new Packet(MemCmd::WriteReq, 0x2000);
    bridge.translateToDevice(pkt);
    EXPECT_EQ(pkt->cmd, MemCmd::ReadUniqueReq);

    pkt = new Packet(MemCmd::UpgradeReq, 0x3000);
    bridge.translateToDevice(pkt);
    EXPECT_EQ(pkt->cmd, MemCmd::ReadUniqueReq);
}

TEST(CXLCacheBridge, TranslateToHost)
{
    DummyParams params;
    CXLCacheBridge bridge(params);

    auto pkt = new Packet(MemCmd::WritebackDirty, 0x3000);
    bridge.translateToHost(pkt);
    EXPECT_EQ(pkt->cmd, MemCmd::WriteResp);

    pkt = new Packet(MemCmd::CleanEvict, 0x4000);
    bridge.translateToHost(pkt);
    EXPECT_EQ(pkt->cmd, MemCmd::WriteCleanResp);
}
