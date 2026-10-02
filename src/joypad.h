#pragma once
#include "msvc-compat/poll.h"
#include "msvc-compat/types.h"
#include <vlc_common.h>

class joypad {
public:
    static bool open(vlc_object_t* owner);
    static void init(const char* host);
    static void getbuttonjoypad();
    static void invalidate(); // Reconnect: discard queued/dequeued old-session commands.
    static void stop(); // Close admission before waiting for the video worker.
    static void close();
};
