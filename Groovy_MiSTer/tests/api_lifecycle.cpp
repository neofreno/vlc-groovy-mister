#include "../api/groovymister.h"
#include <cassert>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
    DWORD handlesBefore = 0, handlesAfter = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handlesBefore);
#endif
    for (int i = 0; i < 100; ++i) {
        GroovyMister* gm = GroovyMister::Create();
        assert(gm->getACK(0) == 0);
        assert(!gm->getPBufferBlit(2));
        gm->PollInputs();
        gm->CmdClose(); gm->CmdClose();
        assert(gm->CmdInit("invalid-ip", 32100, 1, 48000, 2, 0, 1500) == -1);
        gm->CmdClose();
        assert(gm->CmdInit("127.0.0.1", 32100, 1, 48000, 2, 0, 1499) == -1);
        GroovyMister::Destroy(gm);
#ifdef _WIN32
        // Winsock providers may keep process-wide handles after their first use.
        if (i == 9) GetProcessHandleCount(GetCurrentProcess(), &handlesBefore);
#endif
    }
#ifdef _WIN32
    GetProcessHandleCount(GetCurrentProcess(), &handlesAfter);
    printf("Lifecycle handle counts after warmup: %lu -> %lu\n", handlesBefore, handlesAfter);
    assert(handlesAfter <= handlesBefore + 2);
    printf("PASS: 100 create/fail/close/destroy cycles; handles %lu -> %lu\n", handlesBefore, handlesAfter);
#else
    puts("PASS: 100 create/fail/close/destroy cycles");
#endif
}
