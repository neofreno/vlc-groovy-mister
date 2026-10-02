#include "../src/stream_startup.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

// Runtime callbacks model the production state and avoid MSVC diagnosing dead
// branches in template instantiations whose test lambdas return constants.
static bool initializeUntilStopped(std::function<bool()> active, std::function<bool()> attempt,
    std::function<bool(unsigned)> wait, std::function<void(const video_stream::StartupRetry&)> failed) {
    return video_stream::initializeUntilStopped(active, attempt, wait, failed);
}

int main() {
    using video_stream::StartupRetry;
    unsigned attempts = 0, failures = 0;
    std::vector<unsigned> delays;
    check(initializeUntilStopped([] { return true; }, [&] { return ++attempts == 12; },
        [&](unsigned delay) { delays.push_back(delay); return true; },
        [&](const StartupRetry&) { ++failures; }), "receiver can appear after the old five-attempt limit");
    check(attempts == 12 && failures == 11 && delays == std::vector<unsigned>{
        100, 200, 400, 800, 1600, 2000, 2000, 2000, 2000, 2000, 2000}, "bounded exponential backoff");

    attempts = 0;
    check(!initializeUntilStopped([] { return false; }, [&] { ++attempts; return true; },
        [](unsigned) { return true; }, [](const StartupRetry&) {}), "already stopped");
    check(attempts == 0, "no API calls after stop");
    bool running = true;
    check(!initializeUntilStopped([&] { return running; }, [&] { running = false; return true; },
        [](unsigned) { return true; }, [](const StartupRetry&) {}), "close during handshake suppresses success");
    running = true;
    check(!initializeUntilStopped([&] { return running; }, [&] { running = false; return false; },
        [](unsigned) { check(false, "no delay after close during failed handshake"); return true; },
        [](const StartupRetry&) { check(false, "no retry report after close"); }), "failed handshake after close");

    attempts = 0;
    check(!initializeUntilStopped([] { return true; }, [&] { ++attempts; return false; },
        [](unsigned) { return false; }, [](const StartupRetry&) {}), "wait cancellation exits worker");
    check(attempts == 1, "cancelled wait never starts another attempt");
    delays.clear();
    check(initializeUntilStopped([] { return true; }, [] { return true; },
        [&](unsigned delay) { delays.push_back(delay); return true; },
        [](const StartupRetry&) { check(false, "immediate success never reports failure"); }), "immediate success");
    check(delays.empty(), "no added delay on normal startup");

    StartupRetry retry;
    unsigned logs = 0;
    for (unsigned i = 0; i < 10000; ++i) {
        retry.failed();
        if (retry.shouldLog()) ++logs;
        check(retry.delay_ms >= 100 && retry.delay_ms <= 2000, "sustained failure cannot busy-spin");
    }
    check(logs == 1006, "rate-limited retry logging");
    retry.failures = (std::numeric_limits<uint64_t>::max)();
    retry.failed();
    check(retry.failures == (std::numeric_limits<uint64_t>::max)() && retry.delay_ms == 2000,
        "counter and backoff do not wrap");

    // Real condition variable, same stop predicate as production. Synchronize
    // with the waiter instead of sleeping; close must wake the capped 2 s wait.
    std::mutex mutex;
    std::condition_variable ready;
    std::promise<void> waiting;
    auto entered = waiting.get_future();
    std::promise<bool> finished;
    auto result = finished.get_future();
    running = true;
    attempts = 0;
    std::thread worker([&] {
        finished.set_value(initializeUntilStopped(
            [&] { std::lock_guard<std::mutex> lock(mutex); return running; },
            [&] { ++attempts; return false; },
            [&](unsigned delay) {
                if (delay < 2000) return true; // Advance early delays without wall time.
                std::unique_lock<std::mutex> lock(mutex);
                waiting.set_value();
                return !ready.wait_for(lock, std::chrono::milliseconds(delay), [&] { return !running; });
            }, [](const StartupRetry&) {}));
    });
    check(entered.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "worker entered capped wait");
    {
        std::lock_guard<std::mutex> lock(mutex);
        running = false;
    }
    ready.notify_all();
    check(result.wait_for(std::chrono::seconds(1)) == std::future_status::ready, "close interrupts capped wait");
    check(!result.get(), "cancelled worker cannot report initialized");
    worker.join();
    check(attempts == 6, "no seventh attempt after close");
    std::puts("PASS: late startup, backoff, cancellation, immediate success, log bounds, overflow and interruptible close");
}
