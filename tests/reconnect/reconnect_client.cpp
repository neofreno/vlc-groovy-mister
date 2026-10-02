#include "groovymister.h"
#include "../../src/stream_health.h"
#include "../../src/stream_startup.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int64_t now() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
int main(int argc, char** argv) {
    if (argc != 3) return 1;
    const uint16_t port = uint16_t(std::atoi(argv[1]));
    const int compression = std::atoi(argv[2]);
    GroovyMister gm;
    video_stream::AckWatchdog watchdog;
    const int64_t deadline = now() + 12000000;
    unsigned reconnects = 0, frame = 0;
    auto init = [&] {
        const bool ok = video_stream::initializeUntilStopped([&] { return now() < deadline; },
            [&] { return gm.CmdInit("127.0.0.1", port, compression, 48000, 2, 0, 1500) == 0; },
            [](unsigned delay) { std::this_thread::sleep_for(std::chrono::milliseconds(delay)); return true; },
            [](const video_stream::StartupRetry&) {});
        if (!ok) return false;
        gm.CmdSwitchres(6.0, 320, 336, 368, 400, 240, 244, 247, 250, 0);
        return gm.getPBufferBlit(0) != nullptr;
    };
    if (!init()) return 2;
    while (now() < deadline) {
        bool lost = watchdog.expired(now(), gm.fpga.frameEcho);
        if (lost) { gm.getACK(0); lost = watchdog.expired(now(), gm.fpga.frameEcho); }
        if (lost) {
            if (++reconnects > 1) return 3;
            watchdog.reset(); frame = 0;
            if (!init()) return 4;
            continue;
        }
        if (reconnects && gm.fpga.frameEcho >= 3) {
            std::puts("PASS: ACK loss -> new INIT/session -> MODE -> fresh video ACKs");
            return 0;
        }
        std::memset(gm.getPBufferBlit(0), reconnects ? 0x55 : 0xAA, 320 * 240 * 3);
        const uint32_t echo = gm.fpga.frameEcho;
        gm.CmdBlit(++frame, 0, 0, 15000, 0);
        watchdog.sent(now(), echo);
        gm.WaitSync();
    }
    return 5;
}
