#pragma once
#include <atomic>
#include "msvc-compat/poll.h"
#include "msvc-compat/types.h"
#include "groovymister_wrapper.h"
#include "defaults.h"
#include "CaptureAudio.h"
#include "joypad.h"
#include <vlc_filter.h>
#include <vlc_vout.h>
#include <algorithm>
#include <vlc_interface.h>

typedef struct {
    vlc_thread_t thread;
    std::atomic<bool> running{false};
} thread_data_t;

class capture {
public:
    static bool init_class(modeline_struct* modln, vlc_object_t* object, bool aspectratio, bool smooth, const char* Host, uint8_t compress, uint8_t rgbM, uint16_t mtus, double videoFps);
    static bool storeFrame(const picture_t* picture, unsigned sarNum, unsigned sarDen);
    static bool validModeline(const modeline_struct& mode);
    static bool changeModeline(const modeline_struct* mode, bool aspectratio, bool smooth);
    static bool groovy_inicialiced();
    static bool close();
};


// Funci�n para limitar valores entre 0 y 255
inline uint8_t clip(int value)
{
    return static_cast<uint8_t>(std::clamp(value, 0, 255));
}


