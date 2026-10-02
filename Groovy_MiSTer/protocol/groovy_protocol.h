#ifndef GROOVY_PROTOCOL_H
#define GROOVY_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace groovy_wire {
static const uint32_t MAGIC = 0x32574d47; // "GMW2", little endian
static const uint8_t VERSION = 2;
static const size_t HEADER = 28;
static const size_t INIT_SIZE = 16;
static const size_t ACK_SIZE = 16;
static const uint16_t MAX_DATAGRAM = 8972; // 9000-byte IP MTU
static const uint32_t MAX_VIDEO = 720 * 576 * 4;
static const uint32_t MAX_AUDIO = 32768;
static const uint32_t TIMEOUT_MS = 80;
static const unsigned FEC_GROUP = 8;
enum Kind { INIT = 0, VIDEO = 1, AUDIO = 2, MODELINE = 3, CLOSE = 4 };
enum Flags { COMPRESSED = 1, PARITY = 128 };

inline uint16_t read16(const void* data) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    return uint16_t(p[0]) | uint16_t(p[1]) << 8;
}
inline uint32_t read32(const void* data) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void write16(void* data, uint16_t n) {
    uint8_t* p = static_cast<uint8_t*>(data); p[0] = uint8_t(n); p[1] = uint8_t(n >> 8);
}
inline void write32(void* data, uint32_t n) {
    uint8_t* p = static_cast<uint8_t*>(data);
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(n >> (8 * i));
}
inline bool newer(uint32_t a, uint32_t b) { return int32_t(a - b) > 0; }
struct Header {
    uint32_t session, transfer, total, frame;
    uint16_t index, vsync;
    uint8_t kind, field, flags;
};
inline void encode(void* data, const Header& h) {
    uint8_t* p = static_cast<uint8_t*>(data);
    write32(p, MAGIC); write32(p + 4, h.session); write32(p + 8, h.transfer);
    write32(p + 12, h.total); write32(p + 16, h.frame);
    write16(p + 20, h.index); write16(p + 22, h.vsync);
    p[24] = h.kind; p[25] = h.field; p[26] = h.flags; p[27] = VERSION;
}
inline bool decode(const void* data, size_t size, Header& h) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    if (size < HEADER || read32(p) != MAGIC || p[27] != VERSION) return false;
    h.session = read32(p + 4); h.transfer = read32(p + 8); h.total = read32(p + 12);
    h.frame = read32(p + 16); h.index = read16(p + 20); h.vsync = read16(p + 22);
    h.kind = p[24]; h.field = p[25]; h.flags = p[26];
    return h.transfer != 0 && !(h.flags & ~(COMPRESSED | PARITY));
}
inline void ack(void* data, uint32_t session, uint32_t transfer, uint8_t kind, uint16_t payload) {
    uint8_t* p = static_cast<uint8_t*>(data);
    write32(p, MAGIC); write32(p + 4, session); write32(p + 8, transfer);
    p[12] = kind; p[13] = VERSION; write16(p + 14, payload);
}
}
#endif
