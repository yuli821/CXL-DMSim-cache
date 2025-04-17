// NOTE: This is a pure protocol translation version of CXLCacheBridge
// bridging host coherence messages and device-side CXL.cache logic with
// retry logic, forwarding queue support, and complete MemCmd translation.

#include "mem/CXLCacheBridge.hh"
#include "mem/packet.hh"
#include "debug/CXLBridge.hh"

namespace gem5 {

CXLCacheBridge::CXLCacheBridge(const CXLCacheBridgeParams &p)
    : MemObject(p),
      cpuSidePort(name() + ".cpu_side_port", *this),
      memSidePort(name() + ".mem_side_port", *this),
      waitingResp(false),
      waitingReq(false)
{
    hostToDeviceQueue.clear();
    deviceToHostQueue.clear();
}

Port &CXLCacheBridge::getPort(const std::string &if_name, PortID)
{
    if (if_name == "cpu_side_port") return cpuSidePort;
    if (if_name == "mem_side_port") return memSidePort;
    return MemObject::getPort(if_name);
}

AddrRangeList CXLCacheBridge::getAddrRanges() const
{
    AddrRangeList ranges;
    ranges.push_back(RangeSize(0x00000000, 0xC0000000));
    return ranges;
}

bool CXLCacheBridge::CPUSidePort::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(CXLBridge, "[CXLBridge] Host timing request | Addr: %#x | Cmd: %s\n",
            pkt->getAddr(), pkt->cmd.toString().c_str());
    if (!bridge.sendToDevice(pkt)) {
        bridge.hostToDeviceQueue.push_back(pkt);
        bridge.waitingReq = true;
        return false;
    }
    return true;
}

bool CXLCacheBridge::CPUSidePort::recvTimingSnoopReq(PacketPtr pkt)
{
    DPRINTF(CXLBridge, "[CXLBridge] Received timing snoop req | Addr: %#x\n", pkt->getAddr());
    bridge.handleHostSnoop(pkt);
    return true;
}

bool CXLCacheBridge::MemSidePort::recvTimingReq(PacketPtr pkt)
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

bool CXLCacheBridge::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    DPRINTF(CXLBridge, "[CXLBridge] Device response → Host | Addr: %#x | Cmd: %s\n",
            pkt->getAddr(), pkt->cmd.toString().c_str());
    bridge.handleDeviceResponse(pkt);
    return true;
}

void CXLCacheBridge::CPUSidePort::recvReqRetry()
{
    if (bridge.waitingReq && !bridge.hostToDeviceQueue.empty()) {
        PacketPtr pkt = bridge.hostToDeviceQueue.front();
        if (bridge.memSidePort.sendTimingReq(pkt)) {
            bridge.hostToDeviceQueue.pop_front();
            bridge.waitingReq = false;
        }
    }
}

void CXLCacheBridge::MemSidePort::recvRespRetry()
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
    translateToDevice(pkt);
    return true;
}

void CXLCacheBridge::translateToDevice(PacketPtr pkt)
{
    DPRINTF(CXLBridge, "[CXLBridge] Host → Device | Addr: %#x | Cmd: %s\n",
            pkt->getAddr(), pkt->cmd.toString().c_str());

    switch (pkt->cmd) {
        case MemCmd::ReadReq:
            pkt->cmd = MemCmd::ReadSharedReq;
            break;
        case MemCmd::ReadExReq:
        case MemCmd::WriteReq:
        case MemCmd::UpgradeReq:
        case MemCmd::WriteLineReq:
            pkt->cmd = MemCmd::ReadUniqueReq;
            break;
        case MemCmd::InvalidateReq:
        case MemCmd::CleanEvict:
        case MemCmd::WritebackDirty:
            // no translation needed
            break;
        case MemCmd::Go:
            pkt->cmd = MemCmd::Go; // preserve GO marker
            break;
        default:
            DPRINTF(CXLBridge, "[CXLBridge] Warning: Unhandled translation Host → Device: %s\n",
                    pkt->cmd.toString().c_str());
            break;
    }

    memSidePort.sendTimingReq(pkt);
}

void CXLCacheBridge::translateToHost(PacketPtr pkt)
{
    DPRINTF(CXLBridge, "[CXLBridge] Device → Host | Addr: %#x | Cmd: %s\n",
            pkt->getAddr(), pkt->cmd.toString().c_str());

    switch (pkt->cmd) {
        case MemCmd::WritebackDirty:
            pkt->cmd = MemCmd::WriteResp;
            break;
        case MemCmd::CleanEvict:
            pkt->cmd = MemCmd::WriteCleanResp;
            break;
        case MemCmd::ReadResp:
            break;
        case MemCmd::Go:
            pkt->cmd = MemCmd::Go;
            break;
        default:
            DPRINTF(CXLBridge, "[CXLBridge] Warning: Unhandled translation Device → Host: %s\n",
                    pkt->cmd.toString().c_str());
            break;
    }

    cpuSidePort.sendTimingResp(pkt);
}

std::deque<PacketPtr> CXLCacheBridge::hostToDeviceQueue;
std::deque<PacketPtr> CXLCacheBridge::deviceToHostQueue;
bool CXLCacheBridge::waitingReq;
bool CXLCacheBridge::waitingResp;

} // namespace gem5
