#pragma once
#include <cstdint>
#include <limits>

namespace video_stream {

// Delays BETWEEN API attempts; the API owns its bounded handshake timeouts.
struct StartupRetry {
    uint64_t failures = 0;
    unsigned delay_ms = 0;

    void failed() {
        if (failures != (std::numeric_limits<uint64_t>::max)()) ++failures;
        delay_ms = delay_ms == 0 ? 100 : delay_ms >= 1000 ? 2000 : delay_ms * 2;
    }
    bool shouldLog() const { return failures <= 6 || failures % 10 == 0; }
};

// active() is synchronized by the caller. wait() returns false on cancellation
// and must ignore unrelated wakeups. Never retry from the display/audio thread.
template<class Active, class Attempt, class Wait, class Failed>
bool initializeUntilStopped(Active active, Attempt attempt, Wait wait, Failed failed)
{
    StartupRetry retry;
    while (active()) {
        if (attempt()) return active(); // Do not publish a late success after close.
        if (!active()) return false;
        retry.failed();
        failed(retry);
        if (!wait(retry.delay_ms)) return false;
    }
    return false;
}

} // namespace video_stream
