#include "base/unittest.hh"
#include "mem/cache/CXLCacheBridge.hh"
#include "mem/packet.hh"

using namespace gem5;

class DummyParams : public CXLCacheBridgeParams {
public:
    DummyParams() {
        name = "dummy_bridge";
    }
};

TEST(CXLCacheBridge, TranslateToDeviceAndHost)
{
    DummyParams params;
    CXLCacheBridge bridge(params);

    // Translate host ReadReq → ReadSharedReq
    auto read_pkt = new Packet(MemCmd::ReadReq, 0x1000);
    bridge.translateToDevice(read_pkt);
    EXPECT_EQ(read_pkt->cmd, MemCmd::ReadSharedReq);

    // Translate host WriteReq → ReadUniqueReq
    auto write_pkt = new Packet(MemCmd::WriteReq, 0x2000);
    bridge.translateToDevice(write_pkt);
    EXPECT_EQ(write_pkt->cmd, MemCmd::ReadUniqueReq);

    // Translate device WritebackDirty → WriteResp
    auto wb_pkt = new Packet(MemCmd::WritebackDirty, 0x3000);
    bridge.translateToHost(wb_pkt);
    EXPECT_EQ(wb_pkt->cmd, MemCmd::WriteResp);

    // Translate device CleanEvict → WriteCleanResp
    auto clean_pkt = new Packet(MemCmd::CleanEvict, 0x4000);
    bridge.translateToHost(clean_pkt);
    EXPECT_EQ(clean_pkt->cmd, MemCmd::WriteCleanResp);

    // Ensure GO is preserved
    auto go_pkt = new Packet(MemCmd::Go, 0x5000);
    bridge.translateToHost(go_pkt);
    EXPECT_EQ(go_pkt->cmd, MemCmd::Go);

    bridge.translateToDevice(go_pkt);
    EXPECT_EQ(go_pkt->cmd, MemCmd::Go);
} 
