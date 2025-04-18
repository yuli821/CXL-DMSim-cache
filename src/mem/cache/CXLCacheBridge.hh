#ifndef __MEM_CXL_CACHE_BRIDGE_HH__
#define __MEM_CXL_CACHE_BRIDGE_HH__

#include "sim/clocked_object.hh"
#include "mem/port.hh"
#include "mem/packet.hh"
#include <deque>

namespace gem5 {

/**
 * CXLCacheBridge
 * 
 * This bridge implements support for CXL.cache protocol by translating
 * host-originating coherence traffic (ReadReq, ReadExReq, InvalidateReq, etc.)
 * to device-side requests (ReadShared, ReadUnique, etc.), and likewise
 * translating device-initiated requests (e.g., writebacks) to host-visible
 * memory system packets.
 * 
 * The bridge supports backpressure, retry logic, and cycle-based scheduling
 * using ClockedObject for artificial timing delays.
 */
class CXLCacheBridge : public ClockedObject
{
  public:
    CXLCacheBridge(const CXLCacheBridgeParams &p);

    Port &getPort(const std::string &if_name, PortID idx = InvalidPortID) override;

    AddrRangeList getAddrRanges() const override;

  protected:
    /**
     * Slave port connected to host coherence fabric (CoherentXBar)
     */
    class CPUSidePort : public SlavePort {
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

    /**
     * Master port connected to CXL HMC (cache or memory-side model)
     */
    class MemSidePort : public MasterPort {
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

#endif // __MEM_CXL_CACHE_BRIDGE_HH__
