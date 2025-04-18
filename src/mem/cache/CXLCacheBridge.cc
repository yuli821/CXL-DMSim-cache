#include "mem/cxl_cache_bridge.hh"
#include "debug/CXLBridge.hh"
#include "sim/system.hh"
#include "base/trace.hh"
#include "base/logging.hh"
#include "sim/eventq.hh"
#include "sim/sim_object.hh"

namespace gem5 {

CXLCacheBridge::CXLCacheBridge(const CXLCacheBridgeParams &p)
    : ClockedObject(p),
      cpuSidePort(name() + ".cpu_side_port", *this),
      memSidePort(name() + ".mem_side_port", *this)
{
    hostToDeviceQueue.clear();
    deviceToHostQueue.clear();
    waitingReq = false;
    waitingResp = false;
}

Port &CXLCacheBridge::getPort(const std::string &if_name, PortID)
{
    if (if_name == "cpu_side_port") return cpuSidePort;
    if (if_name == "mem_side_port") return memSidePort;
    return ClockedObject::getPort(if_name);
}

AddrRangeList CXLCacheBridge::getAddrRanges() const {
    AddrRangeList ranges;
    ranges.push_back(RangeSize(0x00000000, 0xC0000000));
    return ranges;
}

// ===== CPUSidePort Implementation =====
CXLCacheBridge::CPUSidePort::CPUSidePort(const std::string &name, CXLCacheBridge &b)
    : SlavePort(name, &b), bridge(b) {}

bool CXLCacheBridge::CPUSidePort::recvTimingReq(PacketPtr pkt) {
    return bridge.sendToDevice(pkt);
}

bool CXLCacheBridge::CPUSidePort::recvTimingSnoopReq(PacketPtr pkt) {
    bridge.handleHostSnoop(pkt);
    return true;
}

void CXLCacheBridge::CPUSidePort::recvReqRetry() {
    // Implement retry logic if needed
}

Tick CXLCacheBridge::CPUSidePort::recvAtomic(PacketPtr pkt) {
    return 0; // Not modeled for now
}

void CXLCacheBridge::CPUSidePort::recvFunctional(PacketPtr pkt) {
    // Directly forward functional access to memory
    bridge.memSidePort.sendFunctional(pkt);
}

AddrRangeList CXLCacheBridge::CPUSidePort::getAddrRanges() const {
    return bridge.getAddrRanges();
}

// ===== MemSidePort Implementation =====
CXLCacheBridge::MemSidePort::MemSidePort(const std::string &name, CXLCacheBridge &b)
    : MasterPort(name, &b), bridge(b) {}

bool CXLCacheBridge::MemSidePort::recvTimingReq(PacketPtr pkt) {
    return bridge.cpuSidePort.sendTimingReq(pkt);
}

bool CXLCacheBridge::MemSidePort::recvTimingResp(PacketPtr pkt) {
    bridge.handleDeviceResponse(pkt);
    return true;
}

void CXLCacheBridge::MemSidePort::recvRespRetry() {
    // Implement if needed
}

void CXLCacheBridge::MemSidePort::recvReqRetry() {
    // Implement if needed
}

Tick CXLCacheBridge::MemSidePort::recvAtomic(PacketPtr pkt) {
    return 0;
}

void CXLCacheBridge::MemSidePort::recvFunctional(PacketPtr pkt) {
    bridge.cpuSidePort.sendFunctional(pkt);
}

void CXLCacheBridge::handleHostRequest(PacketPtr pkt) {
    translateToDevice(pkt);
}

void CXLCacheBridge::handleHostSnoop(PacketPtr pkt) {
    translateToDevice(pkt);
}

void CXLCacheBridge::handleDeviceResponse(PacketPtr pkt) {
    translateToHost(pkt);
}

bool CXLCacheBridge::sendToDevice(PacketPtr pkt) {
    schedule(new EventFunctionWrapper([this, pkt]() {
        translateToDevice(pkt);
    }, "DelayedTranslateToDevice"), clockEdge(Cycles(1)));
    return true;
}

void CXLCacheBridge::translateToDevice(PacketPtr pkt) {
    switch (pkt->cmd.toInt()) {
        case MemCmd::ReadReq:
            pkt->cmd = MemCmd::ReadSharedReq;
            break;
        case MemCmd::WriteReq:
        case MemCmd::ReadExReq:
        case MemCmd::UpgradeReq:
        case MemCmd::WriteLineReq:
            pkt->cmd = MemCmd::ReadUniqueReq;
            break;
        default:
            break;
    }
    memSidePort.sendTimingReq(pkt);
}

void CXLCacheBridge::translateToHost(PacketPtr pkt) {
    switch (pkt->cmd.toInt()) {
        case MemCmd::WritebackDirty:
            pkt->cmd = MemCmd::WriteResp;
            break;
        case MemCmd::CleanEvict:
            pkt->cmd = MemCmd::WriteCleanResp;
            break;
        default:
            break;
    }
    cpuSidePort.sendTimingResp(pkt);
}

std::deque<PacketPtr> CXLCacheBridge::hostToDeviceQueue;
std::deque<PacketPtr> CXLCacheBridge::deviceToHostQueue;
bool CXLCacheBridge::waitingReq = false;
bool CXLCacheBridge::waitingResp = false;

} // namespace gem5
