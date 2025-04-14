#ifndef __FAKE_CACHE_HH__
#define __FAKE_CACHE_HH__

#include "mem/mem_object.hh"
#include "mem/packet.hh"
#include "params/FakeCache.hh"
#include "base/statistics.hh"

// Simulates host-side coherence agent tracking CXL HMC metadata
class FakeCache : public MemObject {
  public:

    // CPU-facing port: allows snoops from host caches
    class CpuSidePort : public ResponsePort {
      public:
        FakeCache* owner;
        CpuSidePort(const std::string& name, FakeCache* _owner)
            : ResponsePort(name, _owner), owner(_owner) {}

        AddrRangeList getAddrRanges() const override;
        Tick recvTimingReq(PacketPtr pkt) override;        // host memory access
        Tick recvTimingSnoopReq(PacketPtr pkt) override;   // host-issued snoop
        void recvFunctionalSnoop(PacketPtr pkt) override {} // optional
    };

    // Memory-facing port: sends requests to CXL HMC
    class MemSidePort : public RequestPort {
      public:
        FakeCache* owner;
        MemSidePort(const std::string& name, FakeCache* _owner)
            : RequestPort(name, _owner), owner(_owner) {}

        bool recvTimingResp(PacketPtr pkt) override; // response from HMC
    };

    FakeCache(const Params* p);

    // Expose ports to Python configs
    Port& getPort(const std::string& if_name, PortID idx = InvalidPortID) override;
    AddrRangeList getAddrRanges() const;

    // Handles requests from host memory hierarchy
    Tick handleTimingReq(PacketPtr pkt);

    // Handles snoop requests from host (e.g., invalidate, probe)
    Tick handleSnoop(PacketPtr pkt);

    // Handles responses from CXL HMC
    bool handleTimingResp(PacketPtr pkt);

  protected:
    // MESI-like metadata state
    enum LineState { Invalid, Shared, Exclusive, Modified };
    struct LineInfo {
        LineState state;
        std::vector<uint8_t> data;
    };

    // Cacheline metadata store: addr → (state, data)
    std::map<Addr, LineInfo> cacheLines;

    // Configured physical address ranges this fake cache tracks
    AddrRangeList addrRanges;

    // Send a coherence invalidate to HMC for a line the host wrote
    void sendInvalidateToHMC(Addr addr);

    // Send writeback of modified data to HMC
    void sendWritebackToHMC(Addr addr, const std::vector<uint8_t>& data);

    CpuSidePort cpuPort;
    MemSidePort memPort;

    // Stats interface
    class Stats : public statistics::Group {
      public:
        Stats(FakeCache* cache);
        statistics::Vector accesses;
        statistics::Vector hits;
        statistics::Vector misses;
    };
    Stats stats;
};

#endif // __FAKE_CACHE_HH__
