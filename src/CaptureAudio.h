#pragma once
#include "msvc-compat/poll.h"
#include "msvc-compat/types.h"
#include "defaults.h"
#include "capture.h"
#include <algorithm>
#include <vlc_aout.h>    // Formatos de salida de audio
#include "stream_audio_format.h"

class captureAudio {
public:
    static bool init_audio(uint32_t sndRate, uint8_t snddChan, vlc_object_t* object);
    static video_stream::AudioFormat format();
    static void StoreAudioBuffer(block_t* p_block);
    static void add_audio_to_recording(int64_t videoPTS, int m_field, double videofr,
        video_stream::AudioFormat negotiated);
    static void updateMixAudio(int mixaudio);
    static void change_pause(bool paused, int64_t date);
    static void flush_audio();
    static bool close_audio();
};

