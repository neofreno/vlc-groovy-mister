#ifdef _MSC_VER
#include "../src/msvc-compat/poll.h"
#include "../src/msvc-compat/types.h"
#endif
#include <vlc_common.h>
#include <vlc_es.h>
#include "../src/audio_filter_passthrough.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

static void check(bool ok, const char* what)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}

static bool sameAudio(const audio_format_t& a, const audio_format_t& b)
{
    return a.i_format == b.i_format && a.i_rate == b.i_rate &&
        a.i_physical_channels == b.i_physical_channels && a.i_chan_mode == b.i_chan_mode &&
        a.channel_type == b.channel_type && a.i_bytes_per_frame == b.i_bytes_per_frame &&
        a.i_frame_length == b.i_frame_length && a.i_bitspersample == b.i_bitspersample &&
        a.i_blockalign == b.i_blockalign && a.i_channels == b.i_channels;
}

int main(int argc, char** argv)
{
    // Negative control: the old code accepted the proposed output unchanged.
    const bool simulateOld = argc == 2 && std::strcmp(argv[1], "--simulate-old") == 0;
    for (unsigned inputRate : {44100u, 48000u, 22050u}) {
        for (unsigned deviceRate : {48000u, 44100u, 22050u}) {
            for (uint8_t channels : {uint8_t(2), uint8_t(1), uint8_t(6)}) {
                es_format_t input{}, output{};
                input.i_cat = output.i_cat = AUDIO_ES;
                input.i_codec = input.audio.i_format = VLC_CODEC_FL32;
                input.audio.i_rate = inputRate;
                input.audio.i_channels = channels;
                input.audio.i_physical_channels = uint16_t(channels == 1 ? AOUT_CHAN_CENTER :
                    channels == 2 ? AOUT_CHANS_STEREO : AOUT_CHANS_5_1);
                input.audio.i_chan_mode = AOUT_CHANMODE_DUALMONO;
                input.audio.channel_type = AUDIO_CHANNEL_TYPE_BITMAP;
                input.audio.i_bytes_per_frame = 4u * channels;
                input.audio.i_frame_length = 1;
                input.audio.i_bitspersample = 32;
                input.audio.i_blockalign = 4u * channels;
                output.i_codec = output.audio.i_format = VLC_CODEC_S16N;
                output.audio.i_rate = deviceRate;
                output.audio.i_channels = 2;
                output.audio.i_bitspersample = 16;
                const auto originalInputAudio = input.audio;
                // These buffers belong to different es_format_t instances.
                char inputExtra[3]{}, outputExtra[7]{};
                input.p_extra = inputExtra; input.i_extra = int(sizeof(inputExtra));
                output.p_extra = outputExtra; output.i_extra = int(sizeof(outputExtra));

                if (!simulateOld) audio_stream::preserveFilterFormat(output, input);
                check(output.audio.i_rate == inputRate, "44.1 kHz input must not be labelled 48 kHz");
                check(output.i_codec == input.i_codec && sameAudio(output.audio, input.audio),
                    "codec, rate, channels, layout and sample sizing are unchanged");
                check(sameAudio(input.audio, originalInputAudio), "input format is not modified");
                check(output.p_extra == outputExtra && output.i_extra == int(sizeof(outputExtra)),
                    "output must not alias input-owned format buffers");
                check(output.audio.i_rate != deviceRate || inputRate == deviceRate,
                    "a different device rate remains visible to VLC's downstream resampler");
                const uint64_t duration = uint64_t(inputRate) * 1000000 / output.audio.i_rate;
                check(duration == 1000000, "one second of samples retains one second of duration");
                audio_stream::preserveFilterFormat(output, input);
                check(sameAudio(output.audio, input.audio), "reapplying format is idempotent");
            }
        }
    }
    std::puts("PASS: actual VLC SDK formats, 27 rate/channel combinations, duration and ownership");
}
