#include "../protocol/groovy_resource_lifecycle.h"
#include <cassert>
#include <string>
#include <cstdio>

int main() {
    bool attached, socket, umem, memory, object;
    bool detachOk = true, umemOk = true;
    std::string events;
    auto reset = [&] { attached = socket = umem = memory = object = true; events.clear(); };
    auto stop = [&] {
        return groovy_safe::releaseXdpResources(
            [&] { if (!detachOk) return false; if (attached) { events += 'D'; attached = false; } return true; },
            [&] { assert(!attached); if (socket) { events += 'S'; socket = false; } },
            [&] { assert(!socket); if (!umemOk) return false; if (umem) { events += 'U'; umem = false; } return true; },
            [&] { assert(!umem); if (memory) { events += 'M'; memory = false; } },
            [&] { assert(!memory); if (object) { events += 'B'; object = false; } });
    };
    for (int i = 0; i < 10000; ++i) {
        reset(); assert(stop()); assert(events == "DSUMB");
        assert(stop()); assert(events == "DSUMB"); // repeated load/exec hooks
    }
    reset(); detachOk = false;
    assert(!stop() && events.empty() && socket && umem && memory && object);
    detachOk = true; assert(stop() && events == "DSUMB");
    reset(); umemOk = false;
    assert(!stop() && events == "DS" && umem && memory && object);
    assert(!stop() && events == "DS"); // no double socket close / premature unmap
    umemOk = true; assert(stop() && events == "DSUMB");
    for (unsigned stage = 0; stage <= 5; ++stage) {
        // Startup failed after allocating any prefix of the resource chain.
        reset(); attached = stage >= 1; object = stage >= 1;
        memory = stage >= 2; umem = stage >= 3; socket = stage >= 4;
        assert(stop());
        assert(!attached && !socket && !umem && !memory && !object);
    }
    std::puts("PASS: XDP teardown order, 10000 repeated stops, partial init and safe release retries");
}
