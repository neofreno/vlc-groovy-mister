#include "joypad.h"
#include "joypad_controls.h"
#include "groovymister_wrapper.h"
#include <vlc_playlist.h>
#include <vlc_input.h>
#include <condition_variable>
#include <mutex>
#include <cstring>

namespace {
using joy_control::Action;
vlc_object_t* object = nullptr;
playlist_t* playlist = nullptr;
vlc_thread_t control_thread;
bool started = false;
bool running = false; // queue_mutex
std::mutex queue_mutex;
std::mutex execution_mutex; // Serializes command admission with stop/reconnect.
uint64_t generation = 0; // queue_mutex
std::condition_variable ready;
joy_control::Queue commands;
joy_control::Buttons buttons; // Video/API worker only.
uint32_t last_frame = 0;
uint8_t last_order = 0;
bool received = false;
uint64_t samples = 0, actions = 0, ignored = 0, overflow = 0;

const char* name(Action action) {
    switch (action) {
    case Action::pause: return "play/pause";
    case Action::stop: return "stop";
    case Action::previous: return "previous";
    case Action::next: return "next";
    case Action::forward: return "seek +1s";
    case Action::backward: return "seek -1s";
    case Action::chapter_next: return "chapter/section next";
    case Action::chapter_previous: return "chapter/section previous";
    default: return "none";
    }
}

bool execute(const joy_control::Command& command) {
    struct HeldInput {
        input_thread_t* value = nullptr;
        ~HeldInput() { if (value) vlc_object_release(value); }
    } current; // Destroy after execution_mutex is unlocked on every return path.
    std::lock_guard<std::mutex> execution(execution_mutex);
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (!running || command.generation != generation) return false;
    }
    auto* input = static_cast<input_thread_t*>(command.input);
    const Action action = command.action;
    if (!input) return false;
    // Transport commands address the playlist, so validation + request must
    // be atomic against an input change. Never set input variables under PL lock.
    playlist_Lock(playlist);
    current.value = playlist_CurrentInputLocked(playlist);
    const bool same = current.value == input;
    if (same) {
        switch (action) {
        case Action::pause: playlist_Control(playlist, PLAYLIST_TOGGLE_PAUSE, pl_Locked); break;
        case Action::stop: playlist_Control(playlist, PLAYLIST_STOP, pl_Locked); break;
        case Action::previous: playlist_Control(playlist, PLAYLIST_SKIP, pl_Locked, -1); break;
        case Action::next: playlist_Control(playlist, PLAYLIST_SKIP, pl_Locked, 1); break;
        default: break;
        }
    }
    playlist_Unlock(playlist);
    if (!same) return false;
    bool ok = true;
    switch (action) {
    case Action::pause: case Action::stop: case Action::previous: case Action::next: break;
    default:
        if (!var_GetBool(input, "can-seek")) { ok = false; break; }
        if (action == Action::chapter_next || action == Action::chapter_previous) {
            const int count = var_CountChoices(input, "chapter");
            if (count > 0) {
                const int64_t chapter = var_GetInteger(input, "chapter");
                const int64_t next = chapter + (action == Action::chapter_next ? 1 : -1);
                ok = next >= 0 && next < count && var_SetInteger(input, "chapter", next) == VLC_SUCCESS;
                break;
            }
        }
        {
            const int64_t target = joy_control::seekTarget(var_GetInteger(input, "time"),
                var_GetInteger(input, "length"), action);
            ok = target >= 0 && var_SetInteger(input, "time", target) == VLC_SUCCESS;
        }
        break;
    }
    // Input-specific setters use the retained originating input even if VLC
    // replaces it after the identity check. They can never seek the new input.
    return ok;
}

void* dispatch(void*) {
    for (;;) {
        joy_control::Command command;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            ready.wait(lock, [] { return !running || commands.count != 0; });
            if (!running) return nullptr;
            commands.pop(command);
        }
        // No video/API/queue lock is held while invoking VLC.
        const bool ok = mdate() - command.date <= 250000 && execute(command);
        if (command.input) vlc_object_release(static_cast<input_thread_t*>(command.input));
        if (ok) {
            ++actions;
            msg_Dbg(object, "JoyPad action: %s", name(command.action));
        } else {
            ++ignored;
            msg_Dbg(object, "JoyPad action ignored: %s (stale session/input, closing, boundary or not seekable)", name(command.action));
        }
    }
}
}

bool joypad::open(vlc_object_t* owner) {
    if (started) return true;
    // The vout belongs to an input resource/playlist, not an intf_thread_t.
    for (vlc_object_t* parent = owner->obj.parent; parent; parent = parent->obj.parent) {
        if (!std::strcmp(parent->obj.object_type, "playlist")) {
            playlist = static_cast<playlist_t*>(vlc_object_hold(parent));
            break;
        }
    }
    if (!playlist) {
        msg_Warn(owner, "JoyPad disabled: no owning VLC playlist (embedded LibVLC is not supported)");
        return false;
    }
    object = owner;
    commands.clear(); buttons.reset();
    received = false; samples = actions = ignored = overflow = 0;
    running = true; ++generation;
    if (vlc_clone(&control_thread, dispatch, nullptr, VLC_THREAD_PRIORITY_LOW)) {
        running = false;
        vlc_object_release(playlist); playlist = nullptr; object = nullptr;
        msg_Warn(owner, "JoyPad disabled: cannot start control thread");
        return false;
    }
    started = true;
    msg_Dbg(owner, "JoyPad controller=playlist-v2: input-bound async controls ready");
    return true;
}

void joypad::init(const char* host) {
    if (!started) return;
    gmw_bindInputs(host);
    buttons.reset(); last_frame = 0; last_order = 0; received = false;
    invalidate();
    msg_Dbg(object, "JoyPad input subscription requested: %s:32101 (awaiting packets)", host);
}

void joypad::getbuttonjoypad() {
    if (!started) return;
    gmw_pollInputs(); // Only the video worker accesses the Groovy API.
    gmw_fpgaJoyInputs input{};
    gmw_getJoyInputs(&input);
    if (input.joyFrame != last_frame || input.joyOrder != last_order) {
        last_frame = input.joyFrame; last_order = input.joyOrder; ++samples;
        if (!received) {
            received = true;
            msg_Dbg(object, "JoyPad first packet: joy1=0x%04x joy2=0x%04x", unsigned(input.joy1), unsigned(input.joy2));
        }
    }
    if (!received) return;
    const int64_t now = mdate();
    const Action action = buttons.update(input.joy1 | input.joy2, now);
    if (action == Action::none) return;
    if (action == Action::stop || action == Action::next || action == Action::previous)
        buttons.reset(true);
    uint64_t epoch;
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (!running) return;
        epoch = generation;
    }
    // Snapshot only on an action, not every video field; retain through dispatch.
    input_thread_t* target = playlist_CurrentInput(playlist);
    bool queued = false;
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (running && epoch == generation) {
            queued = commands.push(action, now, target, epoch);
            if (!queued) ++overflow;
        }
    }
    if (queued) ready.notify_one();
    else if (target) vlc_object_release(target);
}

namespace {
void cancelCommands(bool stopping) {
    std::array<joy_control::Command, 16> cancelled{};
    size_t count = 0;
    {
        // Wait for an already-admitted operation, but reject popped commands
        // that have not been admitted. Never hold videoMutex across this gate.
        std::lock_guard<std::mutex> execution(execution_mutex);
        std::lock_guard<std::mutex> lock(queue_mutex);
        ++generation;
        if (stopping) running = false;
        while (commands.pop(cancelled[count])) { if (++count == cancelled.size()) break; }
    }
    // Releasing VLC objects can invoke destruction: always outside mutexes.
    for (size_t i = 0; i < count; ++i)
        if (cancelled[i].input) vlc_object_release(static_cast<input_thread_t*>(cancelled[i].input));
    ready.notify_all();
}
}
void joypad::invalidate() { if (started) cancelCommands(false); }
void joypad::stop() { if (started) cancelCommands(true); }

void joypad::close() {
    // Called after video-worker join: polling cannot race teardown.
    if (!started) return;
    stop();
    vlc_join(control_thread, nullptr);
    msg_Dbg(object, "JoyPad summary: samples=%llu actions=%llu ignored=%llu queue_overflow=%llu",
        samples, actions, ignored, overflow);
    vlc_object_release(playlist);
    playlist = nullptr; object = nullptr; started = false;
}
