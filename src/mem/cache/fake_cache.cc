#include "fake_cache.hh"
#include "debug/Cache.hh"
#include "mem/packet_access.hh"

FakeCache::FakeCache(const Params* p)
    : MemObject(p), stats(this),
      cpuPort(name() + ".cpu_side", this),
      memPort(name() + ".mem_side", this)
{
    // Accepts a configurable address range (same as real cache)
    addrRanges = p->addr_ranges;
}

// Connects fake cache to system ports
Port& FakeCache::getPort(const std::string& if_name, PortID idx) {
    if (if_name == "cpu_side") return cpuPort;
    if (if_name == "mem_side") return memPort;
    return MemObject::getPort(if_name, idx);
}

// Informs the XBar which address ranges this cache tracks
AddrRangeList FakeCache::getAddrRanges() const {
    return addrRanges;
}

// Receives memory request from host (as a responder)
Tick FakeCache::handleTimingReq(PacketPtr pkt) {
    Addr addr = pkt->getAddr() & ~0x3F; // line-aligned
    MemCmd cmd = pkt->cmd;
    stats.accesses[cmd]++;

    auto it = cacheLines.find(addr);
    bool hit = it != cacheLines.end() && it->second.state != Invalid;

    if (pkt->isRead()) {
        if (hit) {
            stats.hits[cmd]++;
            pkt->allocate();
            memcpy(pkt->getPtr<uint8_t>(), it->second.data.data(), pkt->getSize());
            pkt->makeResponse();
        } else {
            stats.misses[cmd]++;
            std::vector<uint8_t> dummy(64, 0xA5); // dummy line content
            cacheLines[addr] = { Shared, dummy };
            pkt->allocate();
            memcpy(pkt->getPtr<uint8_t>(), dummy.data(), pkt->getSize());
            pkt->makeResponse();
        }
    } else if (pkt->isWrite()) {
        stats.misses[cmd]++; // assume write misses
        std::vector<uint8_t> line(64, 0);
        pkt->write(line.data());
        cacheLines[addr] = { Modified, line };
        pkt->makeResponse();
    }

    return curTick();
}

// Receives snoop requests from host (e.g., invalidate, read probe)
Tick FakeCache::handleSnoop(PacketPtr pkt) {
    Addr addr = pkt->getAddr() & ~0x3F;
    auto it = cacheLines.find(addr);
    bool tracked = it != cacheLines.end();

    if (pkt->cmd == MemCmd::InvalidateReq) {
        if (tracked && it->second.state != Invalid) {
            DPRINTF(Cache, "[FakeCache] Invalidating line at %#x\\n", addr);
            it->second.state = Invalid;
            sendInvalidateToHMC(addr); // tell HMC host modified it
        }
    } else if (pkt->isRead() && tracked) {
        if (it->second.state == Modified) {
            // host is trying to read a modified line → write it back
            DPRINTF(Cache, "[FakeCache] Supplying modified line at %#x\\n", addr);
            sendWritebackToHMC(addr, it->second.data);
            it->second.state = Shared;
        }
    }

    return curTick();
}

// Receives response from HMC (e.g., ack to writeback)
bool FakeCache::handleTimingResp(PacketPtr pkt) {
    DPRINTF(Cache, "[FakeCache] Got response from HMC\\n");
    delete pkt;
    return true;
}

// Issues InvalidateReq to HMC when host overwrites shared line
void FakeCache::sendInvalidateToHMC(Addr addr) {
    RequestPtr req = std::make_shared<Request>(addr, 64, 0, 0);
    PacketPtr pkt = new Packet(req, MemCmd::InvalidateReq);
    pkt->allocate();
    bool sent = memPort.sendTimingReq(pkt);
    if (!sent)
        DPRINTF(Cache, "[FakeCache] Invalidate to HMC failed, needs retry\\n");
}

// Issues WritebackDirty to HMC when evicting modified line
void FakeCache::sendWritebackToHMC(Addr addr, const std::vector<uint8_t>& data) {
    RequestPtr req = std::make_shared<Request>(addr, 64, 0, 0);
    PacketPtr pkt = new Packet(req, MemCmd::WritebackDirty);
    pkt->allocate();
    memcpy(pkt->getPtr<uint8_t>(), data.data(), 64);
    bool sent = memPort.sendTimingReq(pkt);
    if (!sent)
        DPRINTF(Cache, "[FakeCache] Writeback to HMC failed, needs retry\\n");
}

// Register per-command access/hit/miss stats
FakeCache::Stats::Stats(FakeCache* cache)
    : statistics::Group(cache),
      accesses(this, "accesses", "Access count per MemCmd"),
      hits(this, "hits", "Hits per MemCmd"),
      misses(this, "misses", "Misses per MemCmd")
{
    accesses.init(MemCmd_NUM_OPS);
    hits.init(MemCmd_NUM_OPS);
    misses.init(MemCmd_NUM_OPS);
}
