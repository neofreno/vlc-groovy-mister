#pragma once
#include <inttypes.h>
#include <vlc_common.h>
#include <vlc_filter.h>

#define DEBUG_GROOVY false
#define DEBUG_GROOVY_VERBOSE true
#define DEBUG_GROOVY_VERBOSE_VIDEO true
#define DEBUG_GROOVY_VERBOSE_AUDIO true
#define DEBUG_GROOVY_VERBOSE_JOYPAD false

#include "video_presets.h"

struct FrameData {
    picture_t* picture;
    int frame_number;
};
