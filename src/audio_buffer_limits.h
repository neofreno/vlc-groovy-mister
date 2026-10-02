#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace audio_stream {
// Prevent unsigned underflow when a PTS jump requests more than is buffered.
inline size_t readableSamples(int64_t requested, int available, int capacity, unsigned channels)
{
    if (requested <= 0 || available <= 0 || capacity <= 0 || !channels) return 0;
    const size_t count = size_t((std::min)(requested, int64_t((std::min)(available, capacity))));
    return count - count % channels;
}
}
