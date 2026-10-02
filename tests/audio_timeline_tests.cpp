#include "../src/audio_timeline.h"
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
int main() {
    using audio_stream::Timeline;
    for (int64_t pause : {100000LL, 5000000LL, 60000000LL, 3600000000LL}) {
        Timeline t;
        t.base=10000000; t.end=12000000; t.last_video=10000000;
        check(t.changePause(true,10000000)==0 && t.paused,"pause starts");
        check(t.readable(10040000,192000,288000,48000,2)==0,"paused queue cannot drain");
        check(t.changePause(true,10020000)==0,"duplicate pause does not reset start date");
        check(t.changePause(false,10000000+pause)==pause,"resume duration");
        check(t.base==10000000+pause && t.end==12000000+pause && t.last_video==10000000+pause,
            "resume shifts buffered dates and last consumed video date together");
        check(t.readable(10040000+pause,192000,288000,48000,2)==3840,
            "first resumed picture uses prebuffered audio without waiting two seconds");
        check(!t.discontinuity(12000000+pause),"first new block remains continuous with shifted queue");
        check(t.changePause(false,10050000+pause)==0,"duplicate resume cannot shift twice");
    }
    Timeline t;
    t.base=1000000; t.end=1020000;
    check(t.readable(1010000,1920,288000,48000,2)==960,"can read inside final block beyond its start");
    check(t.readable(1040000,1920,288000,48000,2)==1920,"video beyond audio end drains available tail");
    check(t.readable(1010000,0,288000,48000,2)==0,"empty read does not mark picture consumed");
    check(t.readable(1010000,1920,288000,48000,2)==960,"same picture retried when samples arrive");
    t.last_video=1010000;
    check(t.readable(1010000,1920,288000,48000,2)==0,"consumed picture is not replayed");
    check(t.readable(999999,1920,288000,48000,2)==0,"future audio held");
    check(!t.discontinuity(1020000) && t.discontinuity(2000000) && t.discontinuity(1),
        "block-end continuity and forward/backward discontinuities");
    t.changePause(true,2000000); t.flush();
    check(t.paused && t.base==0 && t.end==0 && t.last_video==0,"seek flush preserves pause state");
    t.changePause(false,7000000);
    check(t.base==0 && t.end==0,"resume never invents timestamps after seek flush");
    t.base=9000000; t.end=9040000;
    check(t.readable(9020000,3840,288000,48000,2)==1920,"fresh seek timeline can read immediately");
    t.last_video=0;
    for (unsigned i=0;i<1000;++i) {
        const int64_t old=t.base, when=10000000+int64_t(i)*200000;
        t.changePause(true,when); t.changePause(false,when+100000);
        check(t.base==old+100000,"repeated pause cycles shift once");
    }
    t.base=(std::numeric_limits<int64_t>::max)()-2;
    t.changePause(true,1);
    check(t.changePause(false,4)==-1 && t.base==0,"timestamp overflow flushes safely");
    std::puts("PASS: pause/resume rebasing, prebuffer retention, block tail, late-audio retry, seek flush, overflow");
}
