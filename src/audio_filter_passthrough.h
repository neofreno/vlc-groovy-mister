#pragma once

namespace audio_stream {
// filterAudio returns the original VLC block unchanged. Copy only value-owned
// audio metadata: copying an entire es_format_t would alias owned pointers.
// The PCM16/stereo copy sent to MiSTer is independent of this VLC format.
template<class ElementaryFormat>
inline void preserveFilterFormat(ElementaryFormat& output, const ElementaryFormat& input)
{
    output.i_codec = input.i_codec;
    output.audio = input.audio;
}
}
