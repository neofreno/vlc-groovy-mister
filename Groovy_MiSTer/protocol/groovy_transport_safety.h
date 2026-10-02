#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace groovy_safe {
// Legacy commands have no session id. Never let stale packets touch the FPGA
// before a fresh INIT, or interpret media using the screensaver's dimensions.
inline bool legacySessionAllows(uint8_t command, bool connected, bool hasMode) {
    if (command == 2 || command == 8) return true; // INIT / GET_VERSION
    if (!connected) return false;
    if (command == 1 || command == 3 || command == 5) return true; // CLOSE / MODE / STATUS
    return hasMode && (command == 4 || command == 6 || command == 7); // AUDIO / BLIT
}
template<class Ready, class Clock>
bool waitUntil(Ready ready, Clock now, uint64_t timeout) {
    const uint64_t start = now();
    while (!ready()) if (now()-start >= timeout) return false;
    return true;
}
// One owner per UMEM address. RX and TX return the exact completed address.
template<unsigned Count, unsigned Size> struct FramePool {
    uint64_t free[Count];
    bool owned[Count];
    unsigned available;
    void init() {
        available = Count;
        for (unsigned i = 0; i < Count; ++i) { free[i] = uint64_t(i)*Size; owned[i] = false; }
    }
    uint64_t take() {
        if (!available) return UINT64_MAX;
        const uint64_t addr = free[--available];
        owned[addr / Size] = true;
        return addr;
    }
    bool validPacket(uint64_t addr, size_t len) const {
        // Aligned UMEM chunks still have RX data after kernel headroom.
        return addr / Size < Count && owned[addr / Size] && len <= Size - addr % Size;
    }
    bool releasePacket(uint64_t addr) {
        return release(addr - addr % Size);
    }
    bool release(uint64_t addr) {
        if (addr % Size || addr / Size >= Count || !owned[addr / Size]) return false;
        owned[addr / Size] = false;
        free[available++] = addr;
        return true;
    }
};

struct Datagram { size_t offset = 0, size = 0; uint16_t port = 0; };
inline unsigned be16(const uint8_t* p) { return unsigned(p[0])*256 + p[1]; }
inline bool ethernetUDP(const uint8_t* p, size_t len, Datagram& out) {
    if (!p || len < 42 || be16(p+12) != 0x0800 || p[14] != 0x45 ||
        p[23] != 17 || (be16(p+20) & 0x3fff)) return false;
    const unsigned ip = be16(p+16), udp = be16(p+38);
    if (ip < 28 || ip > len-14 || udp < 8 || udp != ip-20) return false;
    out = {42, udp-8u, uint16_t(be16(p+36))};
    return true;
}

// Legacy cannot detect full-sized duplicate/reordered packets (no sequence ID).
// It can still bound memory, expire incomplete transfers and withhold partial data.
class LegacyTransfer {
    bool active_ = false;
    size_t expected = 0, received = 0, payload = 0;
    uint64_t started = 0;
public:
    std::vector<uint8_t> data;
    static constexpr unsigned timeout_ms = 80;
    bool active() const { return active_; }
    void reset() { active_ = false; received = expected = 0; }
    bool begin(size_t total, size_t limit, size_t chunk, uint64_t now) {
        reset();
        if (!total || total > limit || !chunk) return false;
        data.resize(total); expected = total; payload = chunk; started = now; active_ = true;
        return true;
    }
    bool expire(uint64_t now) {
        if (active_ && now-started >= timeout_ms) { reset(); return true; }
        return false;
    }
    bool accepts(size_t size) const {
        if (!active_) return false;
        // Legacy INIT carries no sender MTU. Learn a larger chunk from its first
        // datagram; the receiver's interface MTU does not dictate sender chunks.
        return size == std::min(payload, expected-received) ||
            (!received && size >= payload && size <= 8972 && size <= expected);
    }
    // -1 rejected; 0 pending; 1 complete. Never expose incomplete data to FPGA.
    int add(const uint8_t* bytes, size_t size, uint64_t now) {
        if (expire(now) || !bytes || !accepts(size)) { reset(); return -1; }
        if (!received && size > payload) payload = size;
        memcpy(data.data()+received, bytes, size); received += size;
        if (received != expected) return 0;
        active_ = false;
        return 1;
    }
};
}
