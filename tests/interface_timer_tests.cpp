#include "../src/interface_timer.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <vector>
#include <algorithm>
#include <chrono>

static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
struct TestTimer { void (*callback)(void*); void* data; std::thread worker; bool scheduled = false; };
static std::vector<TestTimer*> live;
static bool fail_create = false;
static unsigned destroyed = 0;
static std::mutex gate_mutex;
static std::condition_variable gate_ready;
static bool entered = false, release_callback = false, destroying = false;

int vlc_timer_create(vlc_timer_t* handle, void (*callback)(void*), void* data) {
    if (fail_create) return -1;
    *handle = new TestTimer{callback, data, {}, false}; live.push_back(*handle); return 0;
}
void vlc_timer_schedule(vlc_timer_t handle, bool absolute, mtime_t delay, mtime_t interval) {
    check(!absolute && delay > 0 && delay == interval, "periodic relative timer");
    handle->scheduled = true;
}
void vlc_timer_destroy(vlc_timer_t handle) {
    check(std::find(live.begin(), live.end(), handle) != live.end(), "destroy own live handle only");
    {
        std::lock_guard<std::mutex> lock(gate_mutex); destroying = true;
    }
    gate_ready.notify_all();
    if (handle->worker.joinable()) handle->worker.join();
    live.erase(std::find(live.begin(), live.end(), handle)); ++destroyed; delete handle;
}
struct State {
    bool alive = true;
    bool block = false;
    int calls = 0;
    int last_mode = -999;
    InterfaceTimer timer;
};
static void callback(void* data) {
    auto* state = static_cast<State*>(data);
    check(state->alive, "owner alive at callback entry");
    if (state->block) {
        std::unique_lock<std::mutex> lock(gate_mutex);
        entered = true; gate_ready.notify_all();
        gate_ready.wait(lock, [] { return release_callback; });
    }
    check(state->alive, "owner alive at callback exit");
    ++state->calls; state->last_mode = 6;
}
int main() {
    State a, b;
    check(a.timer.start(callback, &a, 1000000), "first instance opens");
    TestTimer* first = live.back();
    check(b.timer.start(callback, &b, 1000000), "second instance opens independently");
    TestTimer* second = live.back();
    check(first != second && first->data == &a && second->data == &b, "distinct handles and callback owners");
    first->callback(first->data);
    check(a.calls == 1 && b.calls == 0 && b.last_mode == -999, "independent mode state");
    check(!a.timer.start(callback, &a, 1000000) && live.size() == 2, "same owner cannot overwrite live timer");
    a.timer.stop(); a.alive = false;
    check(live.size() == 1 && live.front() == second, "first close leaves second running");
    second->callback(second->data); check(b.calls == 1, "remaining interface still works");
    a.timer.stop(); b.timer.stop(); check(live.empty() && destroyed == 2, "idempotent close");
    fail_create = true;
    { State failed; check(!failed.timer.start(callback, &failed, 1000000), "creation failure reported"); }
    check(live.empty() && destroyed == 2, "failed timer is never destroyed");
    fail_create = false;
    { State automatic; check(automatic.timer.start(callback, &automatic, 1000000), "RAII start"); }
    check(live.empty() && destroyed == 3, "RAII cleanup");

    State slow; slow.block = true;
    check(slow.timer.start(callback, &slow, 1000000), "in-flight start");
    auto* timer = live.back();
    timer->worker = std::thread([&] { timer->callback(timer->data); });
    {
        std::unique_lock<std::mutex> lock(gate_mutex);
        check(gate_ready.wait_for(lock, std::chrono::seconds(2), [] { return entered; }), "callback entered");
        destroying = false;
    }
    auto closing = std::async(std::launch::async, [&] { slow.timer.stop(); slow.alive = false; });
    {
        std::unique_lock<std::mutex> lock(gate_mutex);
        check(gate_ready.wait_for(lock, std::chrono::seconds(2), [] { return destroying; }), "close entered destroy");
        check(closing.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout, "close waits for callback before owner release");
        release_callback = true;
    }
    gate_ready.notify_all();
    check(closing.wait_for(std::chrono::seconds(2)) == std::future_status::ready, "close completes after callback");
    closing.get();
    check(!slow.alive && slow.calls == 1 && live.empty(), "callback and owner safely released");
    std::puts("PASS: per-instance timers, duplicate start, creation failure, RAII, in-flight callback close");
}
