#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace audio_stream {
// HPS audio region: 32768 bytes. Units below are interleaved PCM16 samples,
// always stereo-aligned. Caller holds the ring lock throughout synchronous emit.
constexpr size_t max_audio_chunk_samples = 16384;

template<class Emit>
size_t submitRing(const int16_t* ring, size_t capacity, size_t start,
    size_t available, size_t requested, int16_t* destination, size_t destination_capacity, Emit emit)
{
    if (!ring || !destination || !capacity || start >= capacity || available > capacity ||
        requested > available || !requested || (capacity | start | available | requested) & 1u ||
        capacity > (std::numeric_limits<size_t>::max)() / sizeof(int16_t)) return 0;
    const size_t chunk = (std::min)(max_audio_chunk_samples, destination_capacity) & ~size_t(1);
    if (!chunk) return 0;
    size_t offset = 0, position = start;
    while (offset < requested) {
        const size_t count = (std::min)(chunk, requested - offset);
        const size_t first = (std::min)(count, capacity - position);
        std::memcpy(destination, ring + position, first * sizeof(int16_t));
        if (count > first)
            std::memcpy(destination + first, ring, (count - first) * sizeof(int16_t));
        // emit must consume/copy the buffer before returning. A void transport
        // call means submission, not receipt. No ownership transfer to emit.
        emit(count);
        offset += count;
        position = count >= capacity - position ? count - (capacity - position) : position + count;
    }
    return requested;
}
}
