#include "../api/groovymister.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>

int main(int argc, char** argv) {
    const char* host = argc > 1 ? argv[1] : "127.0.0.1";
    unsigned port = argc > 2 ? unsigned(atoi(argv[2])) : 43210;
    int compression = argc > 3 ? atoi(argv[3]) : 0;
    int count = argc > 4 ? atoi(argv[4]) : 20;
    int mtu = argc > 5 ? atoi(argv[5]) : 1500;
    GroovyMister* gm = GroovyMister::Create();
    printf("API %s\n", gm->getVersion());
    if (gm->CmdInit(host, uint16_t(port), compression, 48000, 2, 0, uint16_t(mtu))) {
        GroovyMister::Destroy(gm); return 2;
    }
    gm->CmdSwitchres(13.875, 720, 741, 806, 888, 576, 581, 586, 625, 1);
    const unsigned size = 720 * 288 * 3;
    unsigned acked = 0, last = 0;
    for (int frame = 1; frame <= count; ++frame) {
        char* pixels = gm->getPBufferBlit(uint8_t(frame % 2));
        if (!pixels) { GroovyMister::Destroy(gm); return 3; }
        // Compressible moving color bars, also checked byte-for-byte by the mock receiver.
        for (unsigned i = 0; i < size; ++i) pixels[i] = char((i / 360 + frame) % 256);
        gm->CmdBlit(frame, frame % 2, 312, 15000, 0);
        // This fixture verifies payload integrity, not Python thread scheduling.
        // A v2 control ACK does not contain the FPGA's audio-enabled status.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
        do { gm->getACK(5); }
        while (gm->fpga.frameEcho != unsigned(frame) && std::chrono::steady_clock::now() < deadline);
        if (gm->fpga.frameEcho != last) { ++acked; last = gm->fpga.frameEcho; }
        memset(gm->getPBufferAudio(), 0, 3840);
        gm->CmdAudio(3840);
        gm->WaitSync();
    }
    printf("frames=%d acknowledged=%u last=%u gpu=%u synced=%u audio=%u\n", count, acked, last, gm->fpga.frame, gm->fpga.vramSynced, gm->fpga.audio);
    gm->CmdClose(); gm->CmdClose(); // Must be idempotent.
    GroovyMister::Destroy(gm);
    return last ? 0 : 4;
}
