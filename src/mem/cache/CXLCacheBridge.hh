#ifndef __MEM_CXL_CACHE_BRIDGE_HH__
#define __MEM_CXL_CACHE_BRIDGE_HH__

#include "sim/clocked_object.hh"
#include "mem/port.hh"
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

    /**
     * Get access ports
     */
    Port &getPort(const std::string &if_name, PortID idx = InvalidPortID) override;

    /**
     * Report address ranges visible to the coherence fabric (CoherentXBar)
     */
    AddrRangeList getAddrRanges() const override;

  protected:
    /**
     * Slave port connected to host coherence fabric (CoherentXBar)
     */
    class CPUSidePort : public SlavePort {
      private:
        CXLCacheBridge &bridge;
      public:
        CPUSidePort(const std::string &name, CXLCacheBridge &b)
          : SlavePort(name, &b), bridge(b) {}

        bool recvTimingReq(PacketPtr pkt) override;         // Host request
        bool recvTimingSnoopReq(PacketPtr pkt) override;    // Host snoop
        void recvReqRetry() override;                       // Retry if device was busy
    };

    /**
     * Master port connected to CXL HMC (cache or memory-side model)
     */
    class MemSidePort : public MasterPort {
      private:
        CXLCacheBridge &bridge;
      public:
        MemSidePort(const std::string &name, CXLCacheBridge &b)
          : MasterPort(name, &b), bridge(b) {}

        bool recvTimingReq(PacketPtr pkt) override;         // Device writeback or eviction
        bool recvTimingResp(PacketPtr pkt) override;        // Response from host memory
        void recvRespRetry() override;                      // Retry if host was busy
    };

    // Internal handlers for packet routing
    void handleHostRequest(PacketPtr pkt);
    void handleHostSnoop(PacketPtr pkt);
    void handleDeviceResponse(PacketPtr pkt);

    // Translation functions between classic MemCmd and CXL.cache semantics
    bool sendToDevice(PacketPtr pkt);
    void translateToDevice(PacketPtr pkt);
    void translateToHost(PacketPtr pkt);

    // Bridge ports
    CPUSidePort cpuSidePort;
    MemSidePort memSidePort;

    // Internal queues and backpressure flags
    static std::deque<PacketPtr> hostToDeviceQueue;
    static std::deque<PacketPtr> deviceToHostQueue;
    static bool waitingReq;
    static bool waitingResp;
};

} // namespace gem5

#endif // __MEM_CXL_CACHE_BRIDGE_HH__
