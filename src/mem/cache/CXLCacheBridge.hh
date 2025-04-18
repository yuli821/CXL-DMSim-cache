#ifndef __MEM_CACHE_CXL_CACHE_BRIDGE_HH__
#define __MEM_CACHE_CXL_CACHE_BRIDGE_HH__

#include "mem/qport.hh"
#include "mem/packet.hh"
#include "sim/clocked_object.hh"
#include <deque>

namespace gem5 {

class CXLCacheBridge : public ClockedObject
{
  public:
    CXLCacheBridge(const CXLCacheBridgeParams &p);

    Port &getPort(const std::string &if_name, PortID idx = InvalidPortID) override;
    AddrRangeList getAddrRanges() const override;

  protected:
    class CPUSidePort : public ResponsePort
    {
      private:
        CXLCacheBridge &bridge;
      public:
        CPUSidePort(const std::string &name, CXLCacheBridge &b);

        bool recvTimingReq(PacketPtr pkt) override;
        bool recvTimingSnoopReq(PacketPtr pkt) override;
        void recvReqRetry() override;
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        AddrRangeList getAddrRanges() const override;
    };

    class MemSidePort : public RequestPort
    {
      private:
        CXLCacheBridge &bridge;
      public:
        MemSidePort(const std::string &name, CXLCacheBridge &b);

        bool recvTimingReq(PacketPtr pkt) override;
        bool recvTimingResp(PacketPtr pkt) override;
        void recvRespRetry() override;
        void recvReqRetry() override;
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
    };

    void handleHostRequest(PacketPtr pkt);
    void handleHostSnoop(PacketPtr pkt);
    void handleDeviceResponse(PacketPtr pkt);

    bool sendToDevice(PacketPtr pkt);
    void translateToDevice(PacketPtr pkt);
    void translateToHost(PacketPtr pkt);

    CPUSidePort cpuSidePort;
    MemSidePort memSidePort;

    static std::deque<PacketPtr> hostToDeviceQueue;
    static std::deque<PacketPtr> deviceToHostQueue;
    static bool waitingReq;
    static bool waitingResp;
};

} // namespace gem5

#endif // __MEM_CACHE_CXL_CACHE_BRIDGE_HH__
