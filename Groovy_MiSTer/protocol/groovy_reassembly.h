#ifndef GROOVY_REASSEMBLY_H
#define GROOVY_REASSEMBLY_H

#include "groovy_protocol.h"
#include <vector>
#include <algorithm>

namespace groovy_wire {
// One independent assembler per media stream. Never exposes an incomplete frame.
class Reassembly {
public:
    Header header = {};
    std::vector<uint8_t> data;
    uint32_t recovered = 0, dropped = 0;
    void reset(uint32_t floor = 0) {
        last = floor; active = false; data.clear(); seen.clear(); parity.clear(); paritySeen.clear();
    }
    void expire(uint64_t now) {
        if (active && now - started >= TIMEOUT_MS) { active = false; ++dropped; }
    }
    bool add(const Header& h, const uint8_t* bytes, size_t size, uint16_t payload, uint64_t now, uint32_t limit) {
        expire(now);
        if (!payload || payload > MAX_DATAGRAM - HEADER || !h.total || h.total > limit) return false;
        const size_t count = (h.total + payload - 1) / payload;
        if (h.index >= count) return false;
        const bool fec = (h.flags & PARITY) != 0;
        const size_t offset = size_t(h.index) * payload;
        const size_t expected = fec ? payload : std::min(size_t(payload), size_t(h.total) - offset);
        if (size != expected || (fec && h.index % FEC_GROUP)) return false;
        if (h.transfer != last) {
            if (!newer(h.transfer, last)) return false;
            if (active) ++dropped;
            last = h.transfer; header = h; header.flags &= ~PARITY;
            data.assign(h.total, 0); seen.assign(count, 0);
            parity.assign(((count + FEC_GROUP - 1) / FEC_GROUP) * payload, 0);
            paritySeen.assign((count + FEC_GROUP - 1) / FEC_GROUP, 0);
            received = 0; started = now; active = true;
        }
        if (!active || h.session != header.session || h.total != header.total || h.frame != header.frame ||
            h.kind != header.kind || h.field != header.field || h.vsync != header.vsync ||
            (h.flags & ~PARITY) != header.flags) return false;
        const size_t group = h.index / FEC_GROUP;
        if (fec) {
            if (!paritySeen[group]) {
                memcpy(&parity[group * payload], bytes, size); paritySeen[group] = 1;
            }
        } else if (!seen[h.index]) {
            memcpy(&data[offset], bytes, size); seen[h.index] = 1; ++received;
        }
        if (paritySeen[group]) {
            const size_t first = group * FEC_GROUP, end = std::min(first + FEC_GROUP, count);
            size_t missing = count, missingCount = 0;
            for (size_t i = first; i < end; ++i) if (!seen[i]) { missing = i; ++missingCount; }
            if (missingCount == 1) {
                const size_t n = std::min(size_t(payload), size_t(h.total) - missing * payload);
                for (size_t j = 0; j < n; ++j) {
                    uint8_t value = parity[group * payload + j];
                    for (size_t i = first; i < end; ++i)
                        if (i != missing && i * payload + j < data.size()) value ^= data[i * payload + j];
                    data[missing * payload + j] = value;
                }
                seen[missing] = 1; ++received; ++recovered;
            }
        }
        if (received != count) return false;
        active = false;
        return true;
    }
private:
    uint32_t last = 0;
    bool active = false;
    uint64_t started = 0;
    size_t received = 0;
    std::vector<uint8_t> seen, parity, paritySeen;
};
}
#endif
