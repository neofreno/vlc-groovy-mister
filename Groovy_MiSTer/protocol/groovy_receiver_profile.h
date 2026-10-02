#pragma once

#if defined(_AF_XDP) && defined(_WIFI_MODE)
#error "XDP and Wi-Fi are different receiver profiles"
#endif

namespace groovy_receiver {
struct Profile {
    const char* name;
    bool xdp, wifi;
    int udp_buffer, ip_tos;
    bool networkReady(bool ethernet, bool wireless) const { return xdp ? ethernet : ethernet || wireless; }
    bool configureEthernet(bool ethernet) const { return !wifi && ethernet; }
};
#if defined(_AF_XDP)
static constexpr Profile profile{"xdp", true, false, 0, 1};
static constexpr const char* profile_id = "Groovy receiver profile=xdp";
#elif defined(_WIFI_MODE)
static constexpr Profile profile{"wifi", false, true, 8 * 1024 * 1024, 0xB8};
static constexpr const char* profile_id = "Groovy receiver profile=wifi";
#else
static constexpr Profile profile{"standard", false, false, 2 * 1024 * 1024, 1};
static constexpr const char* profile_id = "Groovy receiver profile=standard";
#endif
static constexpr unsigned receive_batch = 64;
static constexpr unsigned poll_batches = 4;
static constexpr const char* evolution = "coherent-20260929";
}
