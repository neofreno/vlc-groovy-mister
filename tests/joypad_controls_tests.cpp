#include "../src/joypad_controls.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <condition_variable>

static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int main() {
    using namespace joy_control;
    check(decode(32) == Action::pause && decode(64) == Action::stop, "pause/stop mapping");
    check(decode(128) == Action::previous && decode(16) == Action::next, "previous/B1 mapping");
    check(decode(8208) == Action::next, "legacy B10+B1 mapping");
    check(decode(3) == Action::none && decode(48) == Action::none, "ambiguous combinations ignored");
    Buttons b;
    check(b.update(32, 0) == Action::pause, "first press without interface");
    for (int i = 1; i < 500; ++i) check(b.update(32, i * 20000) == Action::none, "hold does not toggle pause");
    check(b.update(0, 10000000) == Action::none && b.update(32, 10000001) == Action::pause, "resume after release");
    b.reset();
    check(b.update(1, 0) == Action::forward, "initial seek");
    check(b.update(1, 399999) == Action::none && b.update(1, 400000) == Action::forward, "400ms initial repeat delay");
    check(b.update(1, 549999) == Action::none && b.update(1, 550000) == Action::forward, "150ms repeat interval");
    check(b.update(1, 50000000) == Action::forward && b.update(1, 50000001) == Action::none, "no burst after stall");
    b.reset(true);
    check(b.update(16, 0) == Action::none && b.update(16, 9000000) == Action::none, "transport disarmed while held");
    b.update(0, 9000001);
    check(b.update(16, 9000002) == Action::next, "transport rearmed after release");
    check(b.update(8208, 9000003) == Action::none, "same semantic next action not repeated by combo");
    b.reset();
    check(b.update(8, 0) == Action::chapter_next && b.update(8, 1000000) == Action::none, "chapter edge only");
    check(seekTarget(500000, 9000000, Action::backward) == 0, "clamp start");
    check(seekTarget(8500000, 9000000, Action::forward) == 9000000, "clamp end");
    check(seekTarget(1000000, 0, Action::forward) == 2000000, "short seek with unknown length");
    check(seekTarget(1000000, 0, Action::chapter_next) == -1, "unknown duration no modulo zero");
    check(seekTarget(0, 9, Action::chapter_previous) == -1, "tiny duration safe");
    check(seekTarget(-1, 100, Action::forward) == -1, "unknown position safe");
    check(seekTarget(INT64_MAX, 0, Action::forward) == -1, "no integer overflow");
    check(seekTarget(15000000, 100000000, Action::chapter_next) == 20000000, "next ten-percent boundary");
    check(seekTarget(15000000, 100000000, Action::chapter_previous) == 10000000, "previous ten-percent boundary");
    check(seekTarget(10000000, 100000000, Action::chapter_previous) == 0, "previous from exact boundary");
    for (int64_t length = 1; length < 100; ++length)
        for (int64_t time = 0; time <= length + 10; ++time)
            for (auto action : {Action::forward, Action::backward, Action::chapter_next, Action::chapter_previous}) {
                const auto target = seekTarget(time, length, action);
                check(target >= -1 && target <= length, "small-media target bounds");
            }
    Queue q;
    for (unsigned i = 0; i < 16; ++i) check(q.push(Action::pause, i), "bounded queue capacity");
    check(!q.push(Action::stop, 17), "queue cannot allocate/grow on video thread");
    Command cmd;
    for (unsigned i = 0; i < 16; ++i) check(q.pop(cmd) && cmd.date == i, "queue FIFO");
    check(!q.pop(cmd), "empty queue");
    q.push(Action::stop, 1); q.clear(); check(!q.pop(cmd), "reset cancels pending actions");
    std::mutex mutex;
    std::condition_variable ready;
    bool done = false;
    unsigned consumed = 0;
    std::thread reader([&] {
        for (;;) {
            std::unique_lock<std::mutex> lock(mutex);
            ready.wait(lock, [&] { return done || q.count; });
            if (!q.pop(cmd)) { if (done) break; continue; }
            check(cmd.date == consumed++, "concurrent FIFO");
            lock.unlock(); ready.notify_all();
        }
    });
    for (unsigned i = 0; i < 10000; ++i) {
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait(lock, [&] { return q.count < 16; });
        check(q.push(Action::forward, i), "concurrent producer");
        lock.unlock(); ready.notify_all();
    }
    { std::lock_guard<std::mutex> lock(mutex); done = true; }
    ready.notify_all(); reader.join();
    check(consumed == 10000, "all concurrent commands consumed");
    std::puts("PASS: mappings, edges, repeats, safe seeks, reset, bounded concurrent queue");
}
