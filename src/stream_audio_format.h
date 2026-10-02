#pragma once
#include <cstdint>

namespace video_stream {
struct AudioFormat {
    uint32_t rate = 0;
    uint8_t channels = 0;
    bool valid() const {
        return (rate == 22050 || rate == 44100 || rate == 48000) && channels == 2;
    }
    bool operator==(const AudioFormat& other) const {
        return rate == other.rate && channels == other.channels;
    }
    bool operator!=(const AudioFormat& other) const { return !(*this == other); }
};

// No samples are sent while no filter is available. Keep an audio-capable
// session so late 48 kHz stereo needs no new INIT; never label another rate 48k.
inline AudioFormat selectAudioFormat(AudioFormat available, AudioFormat active = {48000, 2}) {
    return available.valid() ? available : active;
}
inline bool needsAudioInit(AudioFormat available, AudioFormat active) {
    return available.valid() && available != active;
}
inline bool canSendAudio(AudioFormat available, AudioFormat active) {
    return available.valid() && available == active;
}
}
