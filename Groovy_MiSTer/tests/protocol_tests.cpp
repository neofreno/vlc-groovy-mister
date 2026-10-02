#include "../protocol/groovy_reassembly.h"
#include <cassert>
#include <cstdio>
#include <random>

using namespace groovy_wire;
struct Packet { Header h; std::vector<uint8_t> bytes; };
static std::vector<Packet> packets(const std::vector<uint8_t>& data, uint32_t id, unsigned payload) {
    std::vector<Packet> out;
    unsigned count = unsigned((data.size() + payload - 1) / payload);
    for (unsigned first = 0; first < count; first += FEC_GROUP) {
        std::vector<uint8_t> parity(payload, 0);
        for (unsigned i = first; i < std::min(first + FEC_GROUP, count); ++i) {
            unsigned n = unsigned(std::min(size_t(payload), data.size() - i * payload));
            Header h = {123, id, uint32_t(data.size()), id + 10, uint16_t(i), 120, VIDEO, 1, COMPRESSED};
            Packet p = {h, std::vector<uint8_t>(data.begin() + i * payload, data.begin() + i * payload + n)};
            for (unsigned j = 0; j < n; ++j) parity[j] ^= p.bytes[j];
            out.push_back(p);
        }
        Header h = {123, id, uint32_t(data.size()), id + 10, uint16_t(first), 120, VIDEO, 1, COMPRESSED | PARITY};
        out.push_back({h, parity});
    }
    return out;
}
int main() {
    Header header = {0x12345678, 3, 9000, 5, 2, 312, VIDEO, 1, COMPRESSED};
    uint8_t wire[HEADER]; encode(wire, header);
    assert(wire[0] == 'G' && wire[1] == 'M' && wire[2] == 'W' && wire[3] == '2');
    assert(wire[4] == 0x78 && wire[7] == 0x12 && wire[27] == VERSION);
    Header decoded; assert(decode(wire, sizeof(wire), decoded)); assert(decoded.session == header.session);
    assert(!decode(wire, HEADER - 1, decoded)); wire[26] = 8; assert(!decode(wire, sizeof(wire), decoded));
    std::mt19937 rng(12345);
    for (unsigned payload : {256u, 1444u, 8944u}) {
        std::vector<uint8_t> original(payload * 19 + 17);
        for (auto& byte : original) byte = uint8_t(rng());
        for (unsigned iteration = 0; iteration < 100; ++iteration) {
            auto stream = packets(original, 1, payload);
            // Remove one data packet per group, including a short last packet.
            stream.erase(std::remove_if(stream.begin(), stream.end(), [](const Packet& p) {
                return !(p.h.flags & PARITY) && (p.h.index == 2 || p.h.index == 9 || p.h.index == 19);
            }), stream.end());
            stream.push_back(stream.front()); stream.push_back(stream.back()); // duplicate media and parity
            std::shuffle(stream.begin(), stream.end(), rng);
            Reassembly r; unsigned completed = 0;
            for (const auto& p : stream) if (r.add(p.h, p.bytes.data(), p.bytes.size(), uint16_t(payload), 1, MAX_VIDEO)) ++completed;
            assert(completed == 1 && r.data == original && r.recovered >= 3);
            // A completed frame cannot be published again by late duplicates.
            for (const auto& p : stream) assert(!r.add(p.h, p.bytes.data(), p.bytes.size(), uint16_t(payload), 2, MAX_VIDEO));
        }
    }
    std::vector<uint8_t> original(256 * 12 + 7, 0x42);
    auto old = packets(original, 1, 256); auto next = packets(original, 2, 256);
    Reassembly r;
    for (const auto& p : old) {
        if (!(p.h.flags & PARITY) && (p.h.index == 1 || p.h.index == 2)) continue;
        assert(!r.add(p.h, p.bytes.data(), p.bytes.size(), 256, 1, MAX_VIDEO));
    }
    r.expire(100);
    for (const auto& p : old) assert(!r.add(p.h, p.bytes.data(), p.bytes.size(), 256, 101, MAX_VIDEO));
    unsigned completed = 0;
    for (const auto& p : next) if (r.add(p.h, p.bytes.data(), p.bytes.size(), 256, 102, MAX_VIDEO)) ++completed;
    assert(completed == 1 && r.data == original && r.dropped == 1);
    for (const auto& p : old) assert(!r.add(p.h, p.bytes.data(), p.bytes.size(), 256, 103, MAX_VIDEO));
    r.reset(10); // A modeline change invalidates all queued media preceding it.
    for (const auto& p : next) assert(!r.add(p.h, p.bytes.data(), p.bytes.size(), 256, 104, MAX_VIDEO));
    auto p = next.front(); p.h.transfer = 11; p.h.total = MAX_VIDEO + 1;
    assert(!r.add(p.h, p.bytes.data(), p.bytes.size(), 256, 104, MAX_VIDEO));
    p = next.front(); p.h.transfer = 11; p.h.index = 65000;
    assert(!r.add(p.h, p.bytes.data(), p.bytes.size(), 256, 104, MAX_VIDEO));
    p = next.front(); p.h.transfer = 11;
    assert(!r.add(p.h, p.bytes.data(), p.bytes.size() - 1, 256, 104, MAX_VIDEO));
    assert(newer(1, 0xffffffff) && !newer(0xffffffff, 1));
    puts("PASS: wire format, 300 loss/reorder/FEC runs, duplicates, expiry, stale frames, bounds, modeline fence, sequence wrap");
}
