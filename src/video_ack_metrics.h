#pragma once
#include "video_metrics.h"
#include <array>

namespace video_stream {
// Observes cached status only. No socket reads and no effect on raster timing.
// An echo confirms that exact submission, not all older frames and not CRT scanout.
class AckMetrics {
    struct Entry {
        uint32_t frame = 0;
        uint64_t sequence = 0;
        int64_t sent_at = 0;
        bool interlaced = false;
        unsigned field = 0;
    };
    static constexpr unsigned capacity = 256;
    std::array<Entry, capacity> entries{};
    unsigned head = 0, size = 0;
public:
    uint64_t tracked = 0, matched = 0, unobserved = 0, overwritten = 0, abandoned = 0;
    uint64_t progressive = 0, fields = 0;
    uint64_t last_sequence = 0;
    uint32_t last_frame = 0;
    unsigned last_field = 0;
    TimingStats observation_delay;

    unsigned pending() const { return size; }
    void sent(uint32_t frame, uint64_t sequence, int64_t at, bool interlaced, unsigned field) {
        if (size == capacity) { head = (head + 1) % capacity; --size; ++overwritten; }
        entries[(head + size) % capacity] = {frame, sequence, at, interlaced, field};
        ++size; ++tracked;
    }
    bool observe(uint32_t echo, int64_t now) {
        for (unsigned i = 0; i < size; ++i) {
            const auto& entry = entries[(head + i) % capacity];
            if (entry.frame != echo) continue;
            ++matched;
            if (entry.interlaced) ++fields; else ++progressive;
            observation_delay.add(entry.sent_at, now);
            last_sequence = entry.sequence; last_frame = echo; last_field = entry.field;
            // The API exposes its latest status, not every ACK it drained.
            // Earlier submissions may be acknowledged but were not observed.
            unobserved += i;
            head = (head + i + 1) % capacity;
            size -= i + 1;
            return true;
        }
        return false; // No outstanding match: duplicate, old/control or unknown echo.
    }
    void newSession() {
        abandoned += size;
        head = size = 0; // Counters remain cumulative across reconnects.
    }
};

struct PeriodicMetrics {
    static constexpr uint64_t interval_us = 30000000;
    int64_t previous = 0;
    explicit PeriodicMetrics(int64_t now) : previous(now) {}
    bool due(int64_t now) {
        if (now < previous) { previous = now; return false; }
        if (uint64_t(now) - uint64_t(previous) < interval_us) return false;
        previous = now; // No catch-up log burst after a long stall.
        return true;
    }
};
}
