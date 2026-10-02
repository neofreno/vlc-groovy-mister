#pragma once
#include "audio_buffer_limits.h"
#include <limits>

namespace audio_stream {
struct Timeline {
    int64_t base = 0, end = 0, last_video = 0;
    bool paused = false;
    int64_t pause_date = 0;
    void flush() { base = end = last_video = 0; }
    // Flushes preserve pause state (e.g. seeking while paused).
    int64_t changePause(bool next, int64_t now) {
        if (now < 0) return 0;
        if (next == paused) return 0;
        paused = next;
        if (next) { pause_date = now; return 0; }
        const int64_t duration = now >= pause_date ? now-pause_date : 0;
        pause_date = 0;
        for (int64_t* date : {&base, &end, &last_video}) {
            if (*date <= 0) continue;
            if (*date > (std::numeric_limits<int64_t>::max)()-duration) { flush(); return -1; }
            *date += duration;
        }
        return duration;
    }
    bool discontinuity(int64_t pts) const {
        // Compare with the END of the last block, not its starting timestamp.
        return end > 0 && (pts >= end ? uint64_t(pts)-uint64_t(end) : uint64_t(end)-uint64_t(pts)) > 200000;
    }
    size_t readable(int64_t video, int available, int capacity, unsigned rate, unsigned channels) const {
        if (paused || base <= 0 || video <= base || video == last_video || !rate || !channels) return 0;
        const uint64_t delta = uint64_t(video)-uint64_t(base);
        const uint64_t bounded = (std::min)(delta, uint64_t(3000000));
        const int64_t requested = int64_t(bounded*rate/1000000)*channels;
        return readableSamples(requested, available, capacity, channels);
    }
};
}
