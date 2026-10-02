#include "../src/stream_health.h"
#include "../src/video_queue.h"
#include "../src/video_presets.h"
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int main() {
    video_stream::AckWatchdog w;
    check(!w.expired(999999999, 0), "no timeout before first media send");
    w.sent(1000000, 5);
    w.sent(1500000, 5);
    check(!w.expired(1999999, 5) && w.expired(2000000, 5), "sends/repeated ACKs do not refresh deadline");
    w.reset(); w.sent(100, 10);
    check(!w.expired(900000, 11), "advancing ACK renews deadline");
    check(w.expired(1900000, 10), "old/reordered ACK cannot refresh deadline");
    w.reset(); w.sent(100, 0xFFFFFFFEu);
    check(!w.expired(900000, 1), "frame wrap counts as progress");
    check(w.expired(1900000, 0xFFFFFFFFu), "pre-wrap ACK is stale");
    w.reset(); w.sent(100, 2000);
    check(w.expired(1000100, 0), "receiver frame reset does not keep old session alive");
    w.reset(); w.sent(10, 0);
    for (unsigned i = 1; i <= 10000; ++i)
        check(!w.expired(10 + int64_t(i) * 20000, i), "sustained healthy stream does not reconnect");

    // Reconnection invalidates pending/late conversions, not their storage.
    video_stream::FrameQueue q;
    check(q.allocate(), "queue allocation");
    int queued = q.reserve(); q.slots[queued].pts = 1;
    check(q.submit(queued), "queue frame");
    int converting = q.reserve(); auto* pixels = q.slots[converting].pixels.get();
    q.changeGeneration();
    check(q.pending() == 0 && q.slots[converting].pixels.get() == pixels &&
        q.slots[converting].owner == video_stream::Ownership::writing, "reconnect flush preserves converter ownership");
    check(!q.submit(converting), "late frame from lost session rejected");
    video_stream::ModeState<modeline_struct> state;
    unsigned switches = 0;
    modeline_struct mode{}; mode.hActive = 320;
    auto apply = [&](const modeline_struct&) { ++switches; return true; };
    check(state.apply(mode, apply) && state.apply(mode, apply) && switches == 1, "same mode cached in session");
    state = {};
    check(state.apply(mode, apply) && switches == 2, "new session must reapply identical mode");
    std::puts("PASS: ACK loss, duplicates, reordering, wrap, reset, queue invalidation and mode reapplication");
}
