// NOTE: This is a clocked version of CXLCacheBridge
// enabling artificial delay injection and timed forwarding behavior.

#include "mem/cache/CXLCacheBridge.hh"
#include "mem/packet.hh"
#include "debug/CXLCacheBridge.hh"

namespace gem5 {

CXLCacheBridge::CXLCacheBridge(const CXLCacheBridgeParams &p)
    : ClockedObject(p),
    cpuSidePort(p.name + ".cpu_side_port", *this, memSidePort,
                ticksToCycles(p.bridge_lat), ticksToCycles(p.proto_proc_lat), p.resp_fifo_depth, p.ranges),
    memSidePort(p.name + ".mem_side_port", *this, cpuSidePort,
                ticksToCycles(p.bridge_lat), ticksToCycles(p.proto_proc_lat), p.req_fifo_depth),      
    stats(*this)
{
}

Port &CXLCacheBridge::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "cpu_side_port") return cpuSidePort;
    if (if_name == "mem_side_port") return memSidePort;
    return ClockedObject::getPort(if_name, idx);
}

void CXLCacheBridge::init()
{
    // make sure both sides are connected and have the same block size
    if (!cpuSidePort.isConnected() || !memSidePort.isConnected())
        fatal("Both ports of a bridge must be connected.\n");
}

CXLCacheBridge::BridgeResponsePort::BridgeResponsePort(const std::string& _name,
                                        CXLBridge& _bridge,
                                        BridgeRequestPort& _memSidePort,
                                        Cycles _bridge_lat, Cycles _proto_proc_lat,
                                        int _resp_limit, std::vector<AddrRange> _ranges)
    : ResponsePort(_name), bridge(_bridge),
      memSidePort(_memSidePort), bridge_lat(_bridge_lat),
      proto_proc_lat(_proto_proc_lat),
      ranges(_ranges.begin(), _ranges.end()),
      outstandingResponses(0), retryReq(false), respQueueLimit(_resp_limit),
      sendEvent([this]{ trySendTiming(); }, _name)
{
    for (auto i=ranges.begin(); i!=ranges.end(); i++) //this range vector should only contains one addrrange, which is the host memory range
        DPRINTF(CXLCacheBridge, "BridgeResponsePort.ranges = %s\n", i->to_string());
    hmc_range = ranges.back();
    DPRINTF(CXLCacheBridge, "cxl_mem_start = 0x%lx, cxl_mem_end = 0x%lx\n", hmc_range.start(), hmc_range.end());
}

AddrRangeList CXLBridge::BridgeResponsePort::getAddrRanges() const
{
    return ranges;
}

bool CXLCacheBridge::BridgeResponsePort::respQueueFull() const
{
    if (outstandingResponses == respQueueLimit) {
        bridge.stats.rspQueFullEvents++;
        return true;
    } else {
        return false;
    }
}

bool CXLCacheBridge::BridgeResponsePort::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(CXLBridge, "[CXLBridge] Host timing request | Addr: %#x | Cmd: %s\n", pkt->getAddr(), pkt->cmd.toString().c_str());
    if (!bridge.sendToDevice(pkt)) {
        bridge.hostToDeviceQueue.push_back(pkt);
        bridge.waitingReq = true;
        return false;
    }
    return true;
}

bool CXLCacheBridge::BridgeResponsePort::recvTimingSnoopResp(PacketPtr pkt)
{
    DPRINTF(CXLBridge, "[CXLBridge] Received timing snoop req | Addr: %#x\n", pkt->getAddr());
    bridge.handleHostSnoop(pkt);
    return true;
}

bool CXLCacheBridge::BridgeResponsePort::recvTimingResp(PacketPtr pkt)
{
    DPRINTF(CXLBridge, "[CXLBridge] Host response from memory → Device | Addr: %#x | Cmd: %s\n",
            pkt->getAddr(), pkt->cmd.toString().c_str());
    bridge.memSidePort.sendTimingResp(pkt);
    return true;
}

void CXLCacheBridge::BridgeResponsePort::recvReqRetry()
{
    if (bridge.waitingReq && !bridge.hostToDeviceQueue.empty()) {
        PacketPtr pkt = bridge.hostToDeviceQueue.front();
        if (bridge.memSidePort.sendTimingReq(pkt)) {
            bridge.hostToDeviceQueue.pop_front();
            bridge.waitingReq = false;
        }
    }
}

AddrRangeList CXLBridge::BridgeRequestPort::getAddrRanges() const
{
    return ranges;
}

bool CXLCacheBridge::BridgeRequestPort::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(CXLBridge, "[CXLBridge] Device request → Host | Addr: %#x | Cmd: %s\n",
            pkt->getAddr(), pkt->cmd.toString().c_str());
    if (!bridge.cpuSidePort.sendTimingReq(pkt)) {
        bridge.deviceToHostQueue.push_back(pkt);
        bridge.waitingResp = true;
        return false;
    }
    return true;
}

bool CXLCacheBridge::BridgeRequestPort::recvTimingResp(PacketPtr pkt)
{
    DPRINTF(CXLBridge, "[CXLBridge] Device response → Host | Addr: %#x | Cmd: %s\n",
            pkt->getAddr(), pkt->cmd.toString().c_str());
    bridge.handleDeviceResponse(pkt);
    return true;
}

void CXLCacheBridge::BridgeRequestPort::recvRespRetry()
{
    if (bridge.waitingResp && !bridge.deviceToHostQueue.empty()) {
        PacketPtr pkt = bridge.deviceToHostQueue.front();
        if (bridge.cpuSidePort.sendTimingResp(pkt)) {
            bridge.deviceToHostQueue.pop_front();
            bridge.waitingResp = false;
        }
    }
}

void CXLCacheBridge::handleHostRequest(PacketPtr pkt)
{
    translateToDevice(pkt);
}

void CXLCacheBridge::handleHostSnoop(PacketPtr pkt)
{
    translateToDevice(pkt);
}

void CXLCacheBridge::handleDeviceResponse(PacketPtr pkt)
{
    translateToHost(pkt);
}

bool CXLCacheBridge::sendToDevice(PacketPtr pkt)
{
    // Artificial delay injection using ClockedObject
    // For example, delay one clock cycle before sending
    schedule(new EventFunctionWrapper([this, pkt]() {
        translateToDevice(pkt);
    }, "DelayedTranslateToDevice"), clockEdge(Cycles(1)));

    return true;
}

void CXLCacheBridge::translateToDevice(PacketPtr pkt)
{
    DPRINTF(CXLBridge, "[CXLBridge] Host → Device | Addr: %#x | Cmd: %s\n",
            pkt->getAddr(), pkt->cmd.toString().c_str());

    // switch (pkt->cmd) {
    //     case MemCmd::ReadReq:
    //         pkt->cmd = MemCmd::ReadSharedReq;
    //         break;
    //     case MemCmd::ReadExReq:
    //     case MemCmd::WriteReq:
    //     case MemCmd::UpgradeReq:
    //     case MemCmd::WriteLineReq:
    //         pkt->cmd = MemCmd::ReadUniqueReq;
    //         break;
    //     case MemCmd::InvalidateReq:
    //     case MemCmd::CleanEvict:
    //     case MemCmd::WritebackDirty:
    //         // no translation needed
    //         break;
    //     case MemCmd::Go:
    //         pkt->cmd = MemCmd::Go;
    //         break;
    //     default:
    //         DPRINTF(CXLBridge, "[CXLBridge] Warning: Unhandled translation Host → Device: %s\n",
    //                 pkt->cmd.toString().c_str());
    //         break;
    // }

    memSidePort.sendTimingReq(pkt);
}

void CXLCacheBridge::translateToHost(PacketPtr pkt)
{
    DPRINTF(CXLBridge, "[CXLBridge] Device → Host | Addr: %#x | Cmd: %s\n",
            pkt->getAddr(), pkt->cmd.toString().c_str());

    // switch (pkt->cmd) {
    //     case MemCmd::WritebackDirty:
    //         pkt->cmd = MemCmd::WriteResp;
    //         break;
    //     case MemCmd::CleanEvict:
    //         pkt->cmd = MemCmd::WriteCleanResp;
    //         break;
    //     case MemCmd::ReadResp:
    //         break;
    //     case MemCmd::Go:
    //         pkt->cmd = MemCmd::Go;
    //         break;
    //     default:
    //         DPRINTF(CXLBridge, "[CXLBridge] Warning: Unhandled translation Device → Host: %s\n",
    //                 pkt->cmd.toString().c_str());
    //         break;
    // }

    cpuSidePort.sendTimingResp(pkt);
}

std::deque<PacketPtr> CXLCacheBridge::hostToDeviceQueue;
std::deque<PacketPtr> CXLCacheBridge::deviceToHostQueue;
bool CXLCacheBridge::waitingReq;
bool CXLCacheBridge::waitingResp;

} // namespace gem5
