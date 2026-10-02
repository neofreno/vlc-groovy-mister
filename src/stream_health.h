#pragma once
#include <cstdint>

namespace video_stream {
// Only advancing video ACKs count as progress. Repeated status/control packets
// must not keep a dead stream alive. Called exclusively by the sender thread.
struct AckWatchdog {
    static constexpr int64_t timeout_us = 1000000;
    bool armed = false;
    uint32_t echo = 0;
    int64_t progress_at = 0;

    void reset() { *this = {}; }
    void sent(int64_t now, uint32_t currentEcho) {
        if (!armed) { armed = true; echo = currentEcho; progress_at = now; }
    }
    bool expired(int64_t now, uint32_t currentEcho) {
        if (!armed) return false;
        const uint32_t delta = currentEcho - echo;
        if (delta && delta < 0x80000000u) { echo = currentEcho; progress_at = now; }
        return now >= progress_at && now - progress_at >= timeout_us;
    }
};
}
