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
    void init() override;

  protected:
    class BridgeRequestPort;
    class DeferredPacket
    {
      public:
        const Tick tick;
        const PacketPtr pkt;
        DeferredPacket(PacketPtr _pkt, Tick _tick) : tick(_tick), pkt(_pkt){ }
    };
    class BridgeResponsePort : public ResponsePort
    {
      private:
        CXLCacheBridge &bridge;
        BridgeRequestPort& memSidePort;
        /** Minimum request delay though this bridge. */
        const Cycles bridge_lat;
        /** Conversion delay of cxl protocol in bridge*/        
        const Cycles proto_proc_lat;
        /** Address ranges to pass through the bridge */
        const AddrRangeList ranges;
        std::deque<DeferredPacket> transmitList;
        /** Counter to track the outstanding responses. */
        unsigned int outstandingResponses;
        /** If we should send a retry when space becomes available. */
        bool retryReq;
        /** Max queue size for reserved responses. */
        unsigned int respQueueLimit;
        /**
         * Upstream caches need this packet until true is returned, so
         * hold it for deletion until a subsequent call
         */
        std::unique_ptr<Packet> pendingDelete;
        /**
         * Is this side blocked from accepting new response packets.
         * @return true if the reserved space has reached the set limit
         */
        bool respQueueFull() const;
        /**
         * Handle send event, scheduled when the packet at the head of
         * the response queue is ready to transmit (for timing
         * accesses only).
         */
        void trySendTiming();
        /** Send event for the response queue. */
        EventFunctionWrapper sendEvent;
        AddrRange hmc_range;
      public:
         /**
         * Constructor for the BridgeResponsePort.
         *
         * @param _name the port name including the owner
         * @param _bridge the structural owner
         * @param _memSidePort the request port on the other
         *                       side of the bridge
         * @param _bridge_lat the delay in cycles from receiving to sending
         * @param _proto_proc_lat the conversion delay of cxl protocol in bridge
         * @param _resp_limit the size of the response queue
         * @param _ranges a number of address ranges to forward
         */
        BridgeResponsePort(const std::string& _name, CXLBridge& _bridge,
                        BridgeRequestPort& _memSidePort, Cycles _bridge_lat, Cycles _proto_proc_lat,
                        int _resp_limit, std::vector<AddrRange> _ranges);
        /**
         * Queue a response packet to be sent out later and also schedule
         * a send if necessary.
         * @param pkt a response to send out after a delay
         * @param when tick when response packet should be sent
         */
        void schedTimingResp(PacketPtr pkt, Tick when);
        /**
         * Retry any stalled request that we have failed to accept at
         * an earlier point in time. This call will do nothing if no
         * request is waiting.
         */
        void retryStalledReq();
      protected:
        bool recvTimingReq(PacketPtr pkt) override;
        bool recvTimingSnoopResp(PacketPtr pkt) override;
        void recvRespRetry() override;
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        AddrRangeList getAddrRanges() const override;
    };

    class BridgeRequestPort : public RequestPort
    {
      private:
        CXLCacheBridge &bridge;
        /**
         * The response port on the other side of the bridge.
         */
        BridgeResponsePort& cpuSidePort;
        /** Minimum delay though this bridge. */
        const Cycles bridge_lat;
        /** Conversion delay of cxl protocol in bridge*/        
        const Cycles proto_proc_lat;
        /** Address ranges to pass through the bridge */
        const AddrRangeList ranges;
        /**
         * Request packet queue. Request packets are held in this
         * queue for a specified delay to model the processing delay
         * of the bridge.  We use a deque as we need to iterate over
         * the items for functional accesses.
         */
        std::deque<DeferredPacket> transmitList;
        /** Max queue size for request packets */
        const unsigned int reqQueueLimit;
        /**
         * Handle send event, scheduled when the packet at the head of
         * the outbound queue is ready to transmit (for timing
         * accesses only).
         */
        void trySendTiming();
        /** Send event for the request queue. */
        EventFunctionWrapper sendEvent;
      public:
        /**
         * Constructor for the BridgeRequestPort.
         *
         * @param _name the port name including the owner
         * @param _bridge the structural owner
         * @param _cpuSidePort the response port on the other side of
         * the bridge
         * @param _bridge_lat the delay in cycles from receiving to sending
         * @param _proto_proc_lat the conversion delay of cxl protocol in bridge
         * @param _req_limit the size of the request queue
         * @param _ranges a number of address ranges to forward
         */
        BridgeRequestPort(const std::string& _name, CXLBridge& _bridge,
                         BridgeResponsePort& _cpuSidePort, Cycles _bridge_lat,
                         Cycles _proto_proc_lat, int _req_limit, std::vector<AddrRange> _ranges);
        /**
         * Is this side blocked from accepting new request packets.
         * @return true if the occupied space has reached the set limit
         */
        bool reqQueueFull() const;
        /**
         * Queue a request packet to be sent out later and also schedule
         * a send if necessary.
         * @param pkt a request to send out after a delay
         * @param when tick when response packet should be sent
         */
        void schedTimingReq(PacketPtr pkt, Tick when);
        /**
         * Check a functional request against the packets in our
         * request queue.
         * @param pkt packet to check against
         * @return true if we find a match
         */
        bool trySatisfyFunctional(PacketPtr pkt);
      protected:
        bool recvTimingResp(PacketPtr pkt) override;
        void recvTimingSnoopReq(PacketPtr pkt) override;
        void recvReqRetry() override;
        AddrRangeList getAddrRanges() const override;
    };

    struct CXLCacheBridgeStats : public statistics::Group
    {
        CXLCacheBridgeStats(CXLCacheBridge &bridge);

        statistics::Scalar reqQueFullEvents;
        statistics::Scalar reqRetryCounts;
        statistics::Scalar rspQueFullEvents;
        statistics::Scalar reqSendFaild;
        statistics::Scalar rspSendFaild;
        statistics::Scalar reqSendSucceed;
        statistics::Scalar rspSendSucceed;
        statistics::Distribution reqQueueLenDist;
        statistics::Distribution rspQueueLenDist;
        statistics::Distribution rspOutStandDist;
        statistics::Distribution reqQueueLatDist;
        statistics::Distribution rspQueueLatDist;
    };

    CXLCacheBridgeStats stats;

    // void handleHostRequest(PacketPtr pkt);
    // void handleHostSnoop(PacketPtr pkt);
    // void handleDeviceResponse(PacketPtr pkt);

    // bool sendToDevice(PacketPtr pkt);
    // void translateToDevice(PacketPtr pkt);
    // void translateToHost(PacketPtr pkt);

    BridgeResponsePort cpuSidePort;
    BridgeRequestPort memSidePort;
};

} // namespace gem5

#endif // __MEM_CACHE_CXL_CACHE_BRIDGE_HH__
