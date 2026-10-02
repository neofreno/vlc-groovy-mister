#include "../src/audio_ring_submit.h"
#include "../src/audio_timeline.h"
#include <array>
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
static std::array<int16_t, 288000> ring;
static std::array<int16_t, 16386> output;

static void transfer(size_t capacity, size_t start, size_t requested, size_t bufferSize) {
    output.fill(-12345);
    size_t emitted = 0, calls = 0;
    const auto n = audio_stream::submitRing(ring.data(), capacity, start, capacity, requested,
        output.data() + 1, bufferSize, [&](size_t count) {
            check(count > 0 && count <= 16384 && count <= bufferSize && count % 2 == 0, "bounded stereo chunk");
            for (size_t i = 0; i < count; ++i)
                check(output[1 + i] == ring[(start + emitted + i) % capacity], "exact legacy sample order");
            emitted += count; ++calls;
        });
    check(n == requested && emitted == requested, "exact requested sample count");
    check(output.front() == -12345 && output[bufferSize + 1] == -12345, "destination guards preserved");
    const size_t chunk = (std::min)(bufferSize, size_t(16384)) & ~size_t(1);
    check(calls == (requested + chunk - 1) / chunk, "same packet splitting regardless of ring wrap");
}
int main() {
    for (size_t i = 0; i < ring.size(); ++i) ring[i] = int16_t(i % 30001);
    for (size_t capacity = 2; capacity <= 32; capacity += 2)
        for (size_t start = 0; start < capacity; start += 2)
            for (size_t requested = 0; requested <= capacity; requested += 2)
                for (size_t chunk : {size_t(2), size_t(6), size_t(7), size_t(32)})
                    transfer(capacity, start, requested, chunk);
    for (size_t start : {size_t(0), size_t(2), size_t(16382), ring.size() - 2})
        for (size_t count : {size_t(2), size_t(16384), size_t(16386), ring.size()})
            transfer(ring.size(), start, count, 16384);
    unsigned calls = 0;
    auto emit = [&](size_t) { ++calls; };
    check(!audio_stream::submitRing(nullptr, 4, 0, 4, 4, output.data(), 4, emit), "null ring");
    check(!audio_stream::submitRing(ring.data(), 4, 0, 4, 4, nullptr, 4, emit), "null API buffer");
    check(!audio_stream::submitRing(ring.data(), 4, 4, 4, 4, output.data(), 4, emit), "invalid read index");
    check(!audio_stream::submitRing(ring.data(), 4, 0, 6, 4, output.data(), 4, emit), "invalid available");
    check(!audio_stream::submitRing(ring.data(), 4, 0, 2, 4, output.data(), 4, emit), "no reading unavailable samples");
    check(!audio_stream::submitRing(ring.data(), 4, 1, 4, 4, output.data(), 4, emit), "unaligned stereo cursor");
    check(!audio_stream::submitRing(ring.data(), 4, 0, 4, 3, output.data(), 4, emit), "odd stereo request");
    check(!audio_stream::submitRing(ring.data(), 4, 0, 4, 4, output.data(), 1, emit), "destination too small");
    check(calls == 0, "invalid inputs never emit");
    for (unsigned rate : {22050u, 44100u, 48000u}) {
        audio_stream::Timeline t;
        t.base = 1000000; t.end = 4000000;
        const size_t capacity = rate * 6u;
        const auto requested = t.readable(3000000, int(capacity), int(capacity), rate, 2);
        transfer(capacity, capacity - 2, requested, 16384);
        const auto oldBase = t.base + int64_t(requested * 1000000 / (rate * 2));
        const auto sent = audio_stream::submitRing(ring.data(), capacity, capacity - 2, capacity,
            requested, output.data(), 16384, [](size_t) {});
        t.base += int64_t(sent * 1000000 / (rate * 2)); t.last_video = 3000000;
        check(t.base == oldBase && !t.readable(3000000, 4, int(capacity), rate, 2), "one rounding, duplicate PTS suppressed");
        t.changePause(true, 3100000);
        check(!t.readable(3200000, 4, int(capacity), rate, 2), "pause preserves queue");
        t.flush();
        check(t.paused && !t.readable(3200000, 4, int(capacity), rate, 2), "flush preserves pause");
    }
    for (unsigned i = 0; i < 10000; ++i) transfer(4096, (i * 2u) % 4096, 1920, 16384);
    std::puts("PASS: direct ring submission, exact legacy bytes/chunks, wrap, stereo alignment, guards and timeline");
}
