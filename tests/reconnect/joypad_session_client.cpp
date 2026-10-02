#include "../../src/groovymister_wrapper.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstring>

int main() {
    for (unsigned phase = 0; phase < 2; ++phase) {
        if (gmw_init("127.0.0.1", 0, 48000, 2, 0, 1500)) return 1;
        gmw_bindInputs("127.0.0.1");
        gmw_bindInputs("127.0.0.1"); // Must not reopen/rebind in the same session.
        gmw_fpgaJoyInputs input{};
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        const uint32_t expected_frame = phase ? 1 : 100000;
        do {
            gmw_pollInputs(); gmw_getJoyInputs(&input);
            if (input.joyFrame == expected_frame) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < until);
        if (input.joyFrame != expected_frame || input.joyOrder != 1) return 2;
        if (input.joy1 != (phase ? GMW_JOY_B2 : GMW_JOY_B4) || input.joy2 != GMW_JOY_RIGHT) return 3;
        if (phase && (input.joy1LXAnalog != 20 || input.joy2RYAnalog != 27)) return 4;
    }
    gmw_close();
    gmw_fpgaJoyInputs input;
    std::memset(&input, 0x7f, sizeof(input));
    gmw_getJoyInputs(&input);
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&input);
    for (size_t i = 0; i < sizeof(input); ++i) if (bytes[i]) return 5;
    gmw_getJoyInputs(nullptr);
    std::puts("PASS: real wrapper/API inputs, digital/analog packets, reconnect counter reset, closed/null reads");
}
