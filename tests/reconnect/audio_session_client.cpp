#include "groovymister.h"
#include "../../src/stream_audio_format.h"
#include "../../src/video_ack_metrics.h"
#include "../../src/audio_ring_submit.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    if (argc != 3) return 1;
    using namespace video_stream;
    GroovyMister gm;
    AudioFormat active{48000, 2};
    AckMetrics metrics;
    std::array<int16_t, 20000> ring{};
    const AudioFormat filters[] = {{}, {48000, 2}, {44100, 2}, {}, {22050, 2}, {48000, 2}};
    unsigned frame = 0;
    for (unsigned phase = 0; phase < 6; ++phase) {
        const auto available = filters[phase];
        if (!phase || needsAudioInit(available, active)) {
            metrics.newSession();
            const auto desired = selectAudioFormat(available, active);
            if (gm.CmdInit("127.0.0.1", uint16_t(std::atoi(argv[1])), std::atoi(argv[2]),
                desired.rate, desired.channels, 0, 1500)) return 2;
            active = desired;
            frame = 0;
            gm.CmdSwitchres(6.0, 320, 336, 368, 400, 240, 244, 247, 250, 0);
        }
        if (!gm.getPBufferBlit(0)) return 3;
        std::memset(gm.getPBufferBlit(0), phase + 1, 320 * 240 * 3);
        gm.CmdBlit(++frame, 0, 0, 15000, 0);
        metrics.sent(frame, phase + 1, 0, false, 0);
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        do { gm.getACK(5); } while (gm.fpga.frameEcho != frame && std::chrono::steady_clock::now() < until);
        if (gm.fpga.frameEcho != frame) return 4;
        if (!metrics.observe(gm.fpga.frameEcho, 1) || metrics.last_sequence != phase + 1) return 6;
        if (metrics.observe(gm.fpga.frameEcho, 2)) return 7; // Cached status is not another ACK.
        if (canSendAudio(available, active)) {
            if (!gm.fpga.audio || !gm.getPBufferAudio()) return 5;
            for (size_t i = 0; i < ring.size(); ++i) ring[i] = int16_t((i * 17) % 16000 + phase * 1000);
            const auto sent = audio_stream::submitRing(ring.data(), ring.size(), ring.size() - 4,
                ring.size(), 19200, reinterpret_cast<int16_t*>(gm.getPBufferAudio()), 16384,
                [&](size_t samples) { gm.CmdAudio(uint16_t(samples * sizeof(int16_t))); });
            if (sent != 19200) return 9;
        }
    }
    gm.CmdClose();
    if (metrics.matched != 6 || metrics.progressive != 6 || metrics.pending()) return 8;
    std::puts("PASS: video-only, late 48k, 44.1k, filter close, 22.05k, 48k");
}
