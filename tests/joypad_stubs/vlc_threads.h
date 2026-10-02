#pragma once
#include <cstdint>
using mtime_t = int64_t;
struct TestTimer;
using vlc_timer_t = TestTimer*;
int vlc_timer_create(vlc_timer_t*, void (*)(void*), void*);
void vlc_timer_schedule(vlc_timer_t, bool, mtime_t, mtime_t);
void vlc_timer_destroy(vlc_timer_t);
