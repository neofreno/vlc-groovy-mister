#include "../protocol/groovy_receiver_profile.h"
#include <cassert>
#include <cstring>
#include <cstdio>

int main() {
    using namespace groovy_receiver;
    assert(receive_batch == 64 && poll_batches == 4);
    assert(receive_batch * poll_batches == 256);
    assert(!profile.networkReady(false, false));
    assert(profile.networkReady(true, false) && profile.networkReady(true, true));
    assert(!profile.configureEthernet(false));
#if defined(_AF_XDP)
    assert(std::strcmp(profile.name, "xdp") == 0 && profile.xdp && !profile.wifi);
    assert(!profile.networkReady(false, true));
    assert(profile.configureEthernet(true));
#elif defined(_WIFI_MODE)
    assert(std::strcmp(profile.name, "wifi") == 0 && !profile.xdp && profile.wifi);
    assert(profile.networkReady(false, true));
    assert(!profile.configureEthernet(true)); // Never manipulate eth0 in Wi-Fi profile.
    assert(profile.udp_buffer == 8 * 1024 * 1024 && profile.ip_tos == 0xB8);
#else
    assert(std::strcmp(profile.name, "standard") == 0 && !profile.xdp && !profile.wifi);
    assert(profile.networkReady(false, true)); // Ordinary UDP can use either interface.
    assert(profile.configureEthernet(true));
    assert(profile.udp_buffer == 2 * 1024 * 1024 && profile.ip_tos == 1);
#endif
    std::printf("PASS: receiver profile %s, transport isolation, network prerequisites and bounded packet budget\n", profile.name);
}
