#pragma once
#include <cstdint>
#include <limits>

namespace video_stream {
// Aggregate only; no allocations or per-frame log traffic.
struct TimingStats {
    uint64_t count = 0, total_us = 0, max_us = 0;
    void add(int64_t start, int64_t end) {
        if (end < start) return;
        const uint64_t elapsed = uint64_t(end)-uint64_t(start);
        // Keep aggregates defined even for corrupt dates / extremely long runs.
        if (count == (std::numeric_limits<uint64_t>::max)() ||
            elapsed > (std::numeric_limits<uint64_t>::max)()-total_us) return;
        ++count; total_us += elapsed;
        if (elapsed > max_us) max_us = elapsed;
    }
    uint64_t average_us() const { return count ? total_us/count : 0; }
};
}
