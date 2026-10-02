#include "../src/stream_audio_format.h"
#include "../src/video_queue.h"
#include "../src/video_presets.h"
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
int main() {
    using namespace video_stream;
    const AudioFormat absent{}, stereo48{48000, 2};
    auto active = selectAudioFormat(absent);
    check(active == stereo48, "video starts without any audio callback");
    check(!canSendAudio(absent, active), "no audio payload without filter");
    check(!needsAudioInit(stereo48, active) && canSendAudio(stereo48, active),
        "late 48k attaches without reinitializing video");
    for (uint32_t from : {22050u, 44100u, 48000u}) {
        for (uint32_t to : {22050u, 44100u, 48000u}) {
            active = {from, 2};
            const AudioFormat available{to, 2};
            check(needsAudioInit(available, active) == (from != to), "rate transition requires INIT exactly once");
            check(canSendAudio(available, active) == (from == to), "mismatched samples cannot be consumed");
            // A failed handshake must leave the negotiated format unchanged.
            const auto desired = selectAudioFormat(available, active);
            check(active.rate == from, "snapshot alone does not commit negotiation");
            active = desired; // Successful handshake.
            check(canSendAudio(available, active) && !needsAudioInit(available, active), "commit enables matching samples");
            check(selectAudioFormat(absent, active) == active && !needsAudioInit(absent, active),
                "filter close does not interrupt video or change its session rate");
        }
    }
    for (AudioFormat bad : {AudioFormat{96000, 2}, {0, 2}, {48000, 0}, {48000, 1}, {48000, 6}})
        check(!bad.valid() && !canSendAudio(bad, stereo48) && !needsAudioInit(bad, stereo48) &&
            selectAudioFormat(bad) == stereo48, "invalid/unrepresentable format cannot leak into INIT/audio");
    active = stereo48;
    for (unsigned i = 0; i < 10000; ++i) {
        AudioFormat next{i % 2 ? 44100u : 48000u, 2};
        active = selectAudioFormat(next, active);
        check(canSendAudio(next, active) && !canSendAudio(absent, active), "repeated open/close transitions");
    }
    // Same reset primitives as reconnect: pending old video invalidated, an
    // in-flight conversion retains its storage, identical mode reapplied.
    FrameQueue queue;
    check(queue.allocate(), "queue allocation");
    const int old = queue.reserve(); check(queue.submit(old), "old frame pending");
    const int writing = queue.reserve();
    auto* pixels = queue.slots[writing].pixels.get();
    queue.changeGeneration();
    check(queue.pending() == 0 && queue.slots[writing].pixels.get() == pixels &&
        !queue.submit(writing), "audio renegotiation cannot publish stale video or free converter buffer");
    ModeState<modeline_struct> modeState;
    modeline_struct mode{}; mode.hActive = 320;
    unsigned modeCalls = 0;
    auto apply = [&](const modeline_struct&) { ++modeCalls; return true; };
    check(modeState.apply(mode, apply), "initial mode");
    modeState = {};
    check(modeState.apply(mode, apply) && modeCalls == 2, "mode is reapplied after new INIT");
    std::puts("PASS: silent startup, late audio, supported rates, failed-handshake gating, close/reopen and media reset");
}
