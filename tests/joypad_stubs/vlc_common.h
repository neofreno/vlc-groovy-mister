#pragma once
// Deliberately minimal VLC test double. The production joypad.cpp is compiled
// unchanged against this header; no installed VLC or MiSTer is used.
#include <atomic>
#include <cstdint>
#include <thread>
struct vlc_object_t {
    struct { vlc_object_t* parent = nullptr; const char* object_type = "test"; } obj;
    std::atomic<int> references{1};
};
using vlc_thread_t = std::thread*;
constexpr int VLC_SUCCESS = 0;
constexpr int VLC_THREAD_PRIORITY_LOW = 0;
void* vlc_object_hold(vlc_object_t*);
void vlc_object_release(vlc_object_t*);
int vlc_clone(vlc_thread_t*, void* (*)(void*), void*, int);
void vlc_join(vlc_thread_t, void**);
int64_t mdate();
void joy_test_log(vlc_object_t*, const char*, ...);
#define msg_Dbg joy_test_log
#define msg_Warn joy_test_log
