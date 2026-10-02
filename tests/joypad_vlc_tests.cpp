#include "../src/joypad.h"
#include "../src/groovymister_wrapper.h"
#include <vlc_input.h>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cstdarg>

static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
namespace {
std::mutex mutex;
std::condition_variable ready;
playlist_t playlist;
input_thread_t input;
input_thread_t other_input;
input_thread_t* active_input = &input;
std::mutex playlist_mutex;
thread_local bool playlist_locked = false;
std::mutex gate_mutex;
std::condition_variable gate_ready;
bool block_dispatch = false, dispatch_entered = false, release_dispatch = false;
bool block_setter = false, setter_entered = false, release_setter = false;
int64_t other_time = 7000000;
gmw_fpgaJoyInputs packet{}; // Only producer uses the fake API.
std::thread::id producer;
unsigned completed = 0, polls = 0, binds = 0;
int paused = 0, stopped = 0, previous = 0, next = 0;
bool has_input = true, seekable = true, clone_fails = false;
int chapters = 0;
int64_t chapter = 0, time_us = 5000000, length_us = 100000000;
std::atomic<int64_t> clock_us{1000000};
void action_thread() { check(std::this_thread::get_id() != producer, "VLC actions must not run on API/video thread"); }
void wait_done(unsigned expected) {
    std::unique_lock<std::mutex> lock(mutex);
    check(ready.wait_for(lock, std::chrono::seconds(2), [&] { return completed >= expected; }), "action completion timeout");
}
void press(uint16_t mask, bool second = false) {
    packet.joy1 = packet.joy2 = 0; ++packet.joyFrame;
    joypad::getbuttonjoypad();
    if (second) packet.joy2 = mask; else packet.joy1 = mask;
    ++packet.joyFrame;
    joypad::getbuttonjoypad();
}
void arm_dispatch_gate() {
    std::lock_guard<std::mutex> lock(gate_mutex);
    block_dispatch = true; dispatch_entered = release_dispatch = false;
}
void await_dispatch_gate() {
    std::unique_lock<std::mutex> lock(gate_mutex);
    check(gate_ready.wait_for(lock, std::chrono::seconds(2), [] { return dispatch_entered; }), "dispatch gate reached");
}
void release_dispatch_gate() {
    { std::lock_guard<std::mutex> lock(gate_mutex); release_dispatch = true; }
    gate_ready.notify_all();
}
void select_input(input_thread_t* next_input) {
    std::lock_guard<std::mutex> lock(playlist_mutex); active_input = next_input;
}
}
void* vlc_object_hold(vlc_object_t* object) { ++object->references; return object; }
void vlc_object_release(vlc_object_t* object) {
    check(!playlist_locked, "references released outside playlist lock");
    check(--object->references >= 1, "unbalanced object release");
}
int vlc_clone(vlc_thread_t* thread, void* (*function)(void*), void* data, int) {
    if (clone_fails) return -1;
    *thread = new std::thread([=] { function(data); }); return 0;
}
void vlc_join(vlc_thread_t thread, void**) { thread->join(); delete thread; }
int64_t mdate() {
    if (std::this_thread::get_id() != producer) {
        std::unique_lock<std::mutex> lock(gate_mutex);
        if (block_dispatch) {
            dispatch_entered = true; gate_ready.notify_all();
            gate_ready.wait(lock, [] { return release_dispatch; });
            block_dispatch = false;
        }
    }
    return clock_us.load();
}
void joy_test_log(vlc_object_t*, const char* format, ...) {
    if (!std::strncmp(format, "JoyPad action", 13)) {
        { std::lock_guard<std::mutex> lock(mutex); ++completed; }
        ready.notify_all();
    }
}
input_thread_t* playlist_CurrentInput(playlist_t* pl) {
    check(pl == &playlist, "owning playlist selected");
    std::lock_guard<std::mutex> lock(playlist_mutex);
    return has_input ? static_cast<input_thread_t*>(vlc_object_hold(active_input)) : nullptr;
}
void playlist_Lock(playlist_t*) { playlist_mutex.lock(); playlist_locked = true; }
void playlist_Unlock(playlist_t*) { playlist_locked = false; playlist_mutex.unlock(); }
input_thread_t* playlist_CurrentInputLocked(playlist_t*) {
    action_thread(); check(playlist_locked, "identity checked under playlist lock");
    return has_input ? static_cast<input_thread_t*>(vlc_object_hold(active_input)) : nullptr;
}
void playlist_Control(playlist_t*, int query, bool locked, ...) {
    action_thread(); check(locked && playlist_locked, "transport validated/applied atomically");
    std::lock_guard<std::mutex> lock(mutex);
    if (query == PLAYLIST_TOGGLE_PAUSE) paused ^= 1;
    else if (query == PLAYLIST_STOP) ++stopped;
    else {
        check(query == PLAYLIST_SKIP, "known transport action");
        va_list args; va_start(args, locked); const int direction = va_arg(args, int); va_end(args);
        if (direction == 1) ++next; else { check(direction == -1, "skip direction"); ++previous; }
    }
}
void playlist_TogglePause(playlist_t*) { action_thread(); std::lock_guard<std::mutex> lock(mutex); paused ^= 1; }
void playlist_Stop(playlist_t*) { action_thread(); std::lock_guard<std::mutex> lock(mutex); ++stopped; }
void playlist_Prev(playlist_t*) { action_thread(); std::lock_guard<std::mutex> lock(mutex); ++previous; }
void playlist_Next(playlist_t*) { action_thread(); std::lock_guard<std::mutex> lock(mutex); ++next; }
bool var_GetBool(input_thread_t*, const char* key) {
    check(!playlist_locked, "input callback outside playlist lock");
    {
        std::unique_lock<std::mutex> lock(gate_mutex);
        if (block_setter) {
            setter_entered = true; gate_ready.notify_all();
            gate_ready.wait(lock, [] { return release_setter; });
            block_setter = false;
        }
    }
    check(!std::strcmp(key, "can-seek"), "seekability checked");
    std::lock_guard<std::mutex> lock(mutex); return seekable;
}
int var_CountChoices(input_thread_t*, const char*) { std::lock_guard<std::mutex> lock(mutex); return chapters; }
int64_t var_GetInteger(input_thread_t* target, const char* key) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!std::strcmp(key, "chapter")) return chapter;
    if (!std::strcmp(key, "length")) return length_us;
    check(!std::strcmp(key, "time"), "known getter"); return target == &input ? time_us : other_time;
}
int var_SetInteger(input_thread_t* target, const char* key, int64_t value) {
    check(!playlist_locked, "input setters outside playlist lock");
    action_thread(); std::lock_guard<std::mutex> lock(mutex);
    if (!std::strcmp(key, "chapter")) { check(value >= 0 && value < chapters, "chapter in bounds"); chapter = value; }
    else { check(!std::strcmp(key, "time") && value >= 0, "valid seek target");
        if (target == &input) time_us = value; else other_time = value; }
    return VLC_SUCCESS;
}
extern "C" void gmw_bindInputs(const char*) {
    check(std::this_thread::get_id() == producer, "API ownership on bind"); ++binds; packet = {};
}
extern "C" void gmw_pollInputs() { check(std::this_thread::get_id() == producer, "API ownership on poll"); ++polls; }
extern "C" void gmw_getJoyInputs(gmw_fpgaJoyInputs* target) { *target = packet; }

int main() {
    producer = std::this_thread::get_id();
    vlc_object_t root, vout, display;
    playlist.obj = {&root, "playlist"}; vout.obj = {&playlist, "vout"}; display.obj = {&vout, "vout display"};
    check(!joypad::open(&root), "no playlist disables controls without crash");
    joypad::init("127.0.0.1"); joypad::getbuttonjoypad(); joypad::close();
    check(!binds && !polls, "disabled controller does not use API");
    clone_fails = true;
    check(!joypad::open(&display) && playlist.references == 1, "thread failure releases playlist");
    clone_fails = false;
    check(joypad::open(&display) && playlist.references == 2, "vout parent works without interface");
    joypad::init("127.0.0.1");
    unsigned done = 0;
    press(GMW_JOY_B2); wait_done(++done); check(paused == 1, "pause");
    for (unsigned i = 0; i < 200; ++i) joypad::getbuttonjoypad();
    press(GMW_JOY_B2, true); wait_done(++done); check(paused == 0, "resume via pad 2 without new video");
    press(GMW_JOY_RIGHT); wait_done(++done); check(time_us == 6000000, "seek +1 second");
    press(GMW_JOY_LEFT); wait_done(++done); check(time_us == 5000000, "seek -1 second");
    press(GMW_JOY_UP); wait_done(++done); check(time_us == 10000000, "next 10 percent");
    press(GMW_JOY_DOWN); wait_done(++done); check(time_us == 0, "previous 10 percent");
    chapters = 2;
    press(GMW_JOY_UP); wait_done(++done); check(chapter == 1, "next chapter");
    press(GMW_JOY_UP); wait_done(++done); check(chapter == 1, "last chapter remains valid");
    press(GMW_JOY_DOWN); wait_done(++done); check(chapter == 0, "previous chapter");
    chapters = 0; length_us = 0;
    press(GMW_JOY_UP); wait_done(++done); check(time_us == 0, "unknown duration ignored safely");
    seekable = false;
    press(GMW_JOY_RIGHT); wait_done(++done); check(time_us == 0, "non-seekable input ignored");
    seekable = true;
    press(GMW_JOY_B1); wait_done(++done); check(next == 1, "B alone advances");
    press(GMW_JOY_B1 | GMW_JOY_B10); wait_done(++done); check(next == 2, "legacy combination");
    press(GMW_JOY_B4); wait_done(++done); check(previous == 1, "previous item");
    press(GMW_JOY_B3); wait_done(++done); check(stopped == 1, "stop dispatched separately");
    has_input = false;
    press(GMW_JOY_B2); wait_done(++done); check(paused == 0, "missing input safe");
    has_input = true;
    joypad::init("127.0.0.1"); // frame counter back to zero after reconnect
    press(GMW_JOY_B2); wait_done(++done); check(paused == 1 && binds == 2, "reconnect receives new low counters");

    // Real command popped, but not admitted: switching A->B invalidates it.
    for (uint16_t mask : {uint16_t(GMW_JOY_RIGHT), uint16_t(GMW_JOY_B2), uint16_t(GMW_JOY_B1), uint16_t(GMW_JOY_B3)}) {
        select_input(&input); arm_dispatch_gate(); press(mask); await_dispatch_gate();
        check(input.references == 2, "popped command retains originating input");
        select_input(&other_input); release_dispatch_gate(); wait_done(++done);
        check(other_time == 7000000 && paused == 1 && next == 2 && stopped == 1, "stale seek/transport cannot affect B");
        check(input.references == 1 && other_input.references == 1, "stale command references released");
    }
    select_input(&input);
    // Switch after identity validation: the setter still targets retained A.
    { std::lock_guard<std::mutex> lock(gate_mutex); block_setter = true; setter_entered = release_setter = false; }
    press(GMW_JOY_RIGHT);
    {
        std::unique_lock<std::mutex> lock(gate_mutex);
        check(gate_ready.wait_for(lock, std::chrono::seconds(2), [] { return setter_entered; }), "setter gate reached");
    }
    select_input(&other_input);
    { std::lock_guard<std::mutex> lock(gate_mutex); release_setter = true; }
    gate_ready.notify_all(); wait_done(++done);
    check(time_us == 1000000 && other_time == 7000000, "mid-action input change never redirects seek");
    select_input(&input);
    // Invalidate an already-popped action and queued references on reconnect.
    arm_dispatch_gate(); press(GMW_JOY_B2); await_dispatch_gate();
    press(GMW_JOY_RIGHT);
    check(input.references == 3, "popped + queued ownership");
    joypad::invalidate();
    check(input.references == 2, "reconnect drains queued references");
    release_dispatch_gate(); wait_done(++done);
    check(paused == 1 && input.references == 1, "dequeued old-generation action rejected");
    // Expiry must also release the command's retained input.
    arm_dispatch_gate(); press(GMW_JOY_B2); await_dispatch_gate();
    clock_us += 250001;
    release_dispatch_gate(); wait_done(++done);
    check(paused == 1 && input.references == 1, "expired action released without execution");
    // Saturation cannot leak retained inputs or allocate an unbounded queue.
    arm_dispatch_gate(); press(GMW_JOY_B2); await_dispatch_gate();
    for (unsigned i = 0; i < 40; ++i) press(GMW_JOY_RIGHT);
    check(input.references == 18, "only popped plus 16 queued references retained");
    joypad::invalidate();
    check(input.references == 2, "full queue cancellation releases every reference");
    release_dispatch_gate(); wait_done(++done);
    check(input.references == 1 && time_us == 1000000, "overflow and cancellation do not execute seeks");
    // Stop before waiting on video/API; reconnect cannot reopen admission.
    arm_dispatch_gate(); press(GMW_JOY_B2); await_dispatch_gate();
    press(GMW_JOY_RIGHT); joypad::stop(); joypad::stop();
    joypad::init("127.0.0.1");
    press(GMW_JOY_B2);
    release_dispatch_gate(); wait_done(++done);
    check(paused == 1 && input.references == 1, "stop cancels popped/queued commands and survives late init");
    joypad::close(); joypad::close();
    check(input.references == 1 && playlist.references == 1 && completed == done, "balanced references, no duplicate actions");
    check(joypad::open(&display), "reopen after close"); joypad::init("127.0.0.1"); joypad::close();
    check(playlist.references == 1, "reopen cleanup");
    std::puts("PASS: production joypad against VLC doubles; async actions, pause, seeks, references, reconnect, teardown");
}
