#pragma once
#include <vlc_threads.h>

// Each interface owns its handle. stop() joins any in-flight callback before
// the callback data/owning VLC object may be destroyed (VLC timer contract).
class InterfaceTimer {
    vlc_timer_t handle{};
    bool active = false;
public:
    InterfaceTimer() = default;
    InterfaceTimer(const InterfaceTimer&) = delete;
    InterfaceTimer& operator=(const InterfaceTimer&) = delete;
    ~InterfaceTimer() { stop(); }
    bool start(void (*callback)(void*), void* data, mtime_t interval) {
        if (active || vlc_timer_create(&handle, callback, data) != 0) return false;
        active = true;
        vlc_timer_schedule(handle, false, interval, interval);
        return true;
    }
    void stop() {
        if (!active) return;
        vlc_timer_destroy(handle);
        active = false;
    }
};
