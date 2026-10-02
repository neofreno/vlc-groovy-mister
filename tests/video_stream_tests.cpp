#include "../src/video_frame.h"
#include "../src/video_queue.h"
#include "../src/audio_buffer_limits.h"
#include "../src/video_mode.h"
#include "../src/video_presets.h"
#include "../src/video_metrics.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>

static void check(bool result, const char* message)
{
    if (!result) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static void conversionTests()
{
    using namespace video_stream;
    // 4x4 image inside wider rows, with separate chroma padding.
    std::array<uint8_t, 32> y{};
    std::array<uint8_t, 8> u{}, v{};
    y.fill(16); u.fill(128); v.fill(128);
    I420View src;
    src.coded_width = src.coded_height = src.width = src.height = 4;
    src.planes[0] = { y.data(), 8, 4 };
    src.planes[1] = { u.data(), 4, 2 };
    src.planes[2] = { v.data(), 4, 2 };
    std::array<uint8_t, 50> dst;
    dst.fill(0xA5);
    check(convert(src, dst.data() + 1, 48, 4, 4, false), "limited black conversion");
    check(dst.front() == 0xA5 && dst.back() == 0xA5, "destination canaries");
    for (size_t i = 1; i < 49; ++i) check(dst[i] == 0, "all pixels initialized, including last");

    y.fill(235);
    check(convert(src, dst.data() + 1, 48, 4, 4, false), "limited white conversion");
    for (size_t i = 1; i < 49; ++i) check(dst[i] == 255, "limited white level");

    // Reference BT.601 limited-range red: Y=81, U=90, V=240.
    y.fill(81); u.fill(90); v.fill(240);
    check(convert(src, dst.data() + 1, 48, 4, 4, false), "red conversion");
    check(dst[1] <= 2 && dst[2] <= 2 && dst[3] >= 253, "wire BGR red channel order");
    y.fill(41); u.fill(240); v.fill(110);
    check(convert(src, dst.data() + 1, 48, 4, 4, false), "blue conversion");
    check(dst[1] >= 253 && dst[2] <= 2 && dst[3] <= 2, "wire BGR blue channel order");
    y.fill(145); u.fill(54); v.fill(34);
    check(convert(src, dst.data() + 1, 48, 4, 4, false), "green conversion");
    check(dst[1] <= 2 && dst[2] >= 253 && dst[3] <= 2, "wire BGR green channel order");

    src.matrix = Matrix::bt709;
    y.fill(63); u.fill(102); v.fill(240);
    check(convert(src, dst.data() + 1, 48, 4, 4, false), "BT709 red");
    check(dst[1] <= 2 && dst[2] <= 2 && dst[3] >= 253, "BT709 matrix");
    src.matrix = Matrix::bt2020;
    y.fill(74); u.fill(97); v.fill(240);
    check(convert(src, dst.data() + 1, 48, 4, 4, false), "BT2020 red");
    check(dst[1] <= 2 && dst[2] <= 2 && dst[3] >= 253, "BT2020 matrix");

    src.full_range = true;
    u.fill(128); v.fill(128); y.fill(0);
    check(convert(src, dst.data() + 1, 48, 4, 4, false), "full-range black conversion");
    for (size_t i = 1; i < 49; ++i) check(dst[i] == 0, "full-range black level");
    y[1 * 8 + 1] = 30; y[1 * 8 + 2] = 90;
    y[2 * 8 + 1] = 160; y[2 * 8 + 2] = 255;
    src.x = src.y = 1; src.width = src.height = 2;
    check(convert(src, dst.data(), 12, 2, 2, false), "odd crop with padding");
    check(dst[0] == 30 && dst[3] == 90 && dst[6] == 160 && dst[9] == 255, "crop and pitch");
    check(convert(src, dst.data(), 27, 3, 3, false), "fractional nearest scaling");
    check(dst[0] == 30 && dst[3] == 30 && dst[6] == 90 && dst[24] == 255,
        "fractional scaling including bottom right");
    u[0] = 240; u[1] = 90;
    check(convert(src, dst.data(), 12, 2, 2, false), "odd chroma crop");
    check(dst[0] > dst[2] && dst[3] < dst[5], "chroma address includes crop offset");
    u.fill(128);

    src.width = 4; check(!valid(src), "reject crop outside coded width"); src.width = 2;
    src.planes[2].pixels = nullptr; check(!valid(src), "reject missing plane"); src.planes[2].pixels = v.data();
    src.planes[1].pitch = 1; check(!valid(src), "reject short chroma pitch"); src.planes[1].pitch = 4;
    src.planes[0].lines = 2; check(!valid(src), "reject short luma plane"); src.planes[0].lines = 4;
    check(!convert(src, dst.data(), 11, 2, 2, false), "reject undersized output");
    check(!convert(src, dst.data(), dst.size(), 0, 2, false), "reject zero dimensions");

    src.x = src.y = 0; src.width = 4; src.height = 2; y.fill(255);
    src.sar_num = 4; src.sar_den = 3;
    check(convert(src, dst.data(), 48, 4, 4, true), "letterbox");
    check(dst[0] == 0 && dst[12] == 255 && dst[36] == 0, "black bars and picture");
}

static void aspectTests()
{
    using namespace video_stream;
    for (const auto size : { DrawSize{320, 240}, DrawSize{320, 480},
                            DrawSize{720, 480}, DrawSize{720, 576}, DrawSize{256, 240} }) {
        auto draw = fitAspect(1920, 1080, 1, 1, size.width, size.height, true);
        check(draw.width == size.width && draw.height == size.height * 3 / 4,
            "16:9 occupies 75 percent CRT height regardless of raster");
        draw = fitAspect(720, 576, 16, 15, size.width, size.height, true);
        check(draw.width == size.width && draw.height == size.height, "PAL anamorphic 4:3 fills CRT");
        draw = fitAspect(720, 576, 64, 45, size.width, size.height, true);
        check(draw.width == size.width && draw.height == size.height * 3 / 4, "PAL anamorphic 16:9");
        draw = fitAspect(720, 480, 8, 9, size.width, size.height, true);
        check(draw.width == size.width && draw.height == size.height, "NTSC anamorphic 4:3");
        draw = fitAspect(720, 480, 32, 27, size.width, size.height, true);
        check(draw.width == size.width && draw.height == size.height * 3 / 4, "NTSC anamorphic 16:9");
        draw = fitAspect(1920, 1080, 1, 1, size.width, size.height, false);
        check(draw.width == size.width && draw.height == size.height, "unchecked aspect stretches");
    }
    auto draw = fitAspect(1080, 1920, 1, 1, 320, 240, true);
    check(draw.width == 135 && draw.height == 240, "portrait pillarbox");
    draw = fitAspect(640, 480, 0, 0, 720, 576, true);
    check(draw.width == 720 && draw.height == 576, "missing SAR defaults to square source pixels");

    std::array<uint8_t, 4> y{235, 235, 235, 235};
    const uint8_t uv = 128;
    I420View src;
    src.width = src.height = src.coded_width = src.coded_height = 2;
    src.sar_num = 16; src.sar_den = 9;
    src.planes[0] = {y.data(), 2, 2};
    src.planes[1] = src.planes[2] = {&uv, 1, 1};
    std::vector<uint8_t> out(720 * 576 * 3 + 2, 0xA5);
    for (int i = 0; i < 4; ++i) {
        const bool keep = (i & 1) != 0;
        check(convert(src, out.data() + 1, out.size() - 2, 720, 576, keep), "toggle aspect conversion");
        for (unsigned row = 0; row < 576; ++row)
            for (unsigned col = 0; col < 720 * 3; ++col)
                check(out[1 + row * 720 * 3 + col] ==
                    (keep && (row < 72 || row >= 504) ? 0 : 255), "PAL bands, no stale pixels on toggle");
        check(out.front() == 0xA5 && out.back() == 0xA5, "aspect buffer canaries");
    }
}

static void modeApplicationTests()
{
    using namespace video_stream;
    struct Mode { double pClock; unsigned hActive, hBegin, hEnd, hTotal, vActive, vBegin, vEnd, vTotal, interlace; };
    const Mode pal{13.875, 720, 741, 806, 888, 576, 581, 586, 625, 1};
    const Mode small{6.7, 320, 336, 367, 426, 240, 244, 247, 262, 0};
    ModeState<Mode> state;
    unsigned switches = 0;
    bool succeeds = true;
    auto apply = [&](const Mode&) { ++switches; return succeeds; };
    FrameQueue queue;
    check(queue.allocate(), "aspect transition queue");
    check(state.apply(pal, apply), "initial hardware mode");
    for (int i = 0; i < 1000; ++i) {
        queue.changeGeneration();
        check(state.apply(pal, apply), "content generation accepted");
    }
    check(switches == 1, "1000 aspect-only generations cause no hardware switches");
    succeeds = false;
    check(!state.apply(small, apply) && switches == 2, "failed mode remains unapplied");
    succeeds = true;
    check(state.apply(pal, apply) && switches == 3, "return to previous mode reapplies after failed switch");
    succeeds = false;
    check(!state.apply(small, apply), "transient failure");
    succeeds = true;
    check(state.apply(small, apply) && switches == 5, "same request retried after failure");
    for (int i = 0; i < 1000; ++i)
        check(state.apply(i & 1 ? small : pal, apply), "repeated large-small hardware transition");
    check(switches == 1005, "exact number of effective timing switches");
}

static void automaticModeTests()
{
    using namespace video_stream;
    // Use the production presets, not a hand-maintained copy of their timings.
    check(selectMode(1920,1034,24,1,defaults_modelines,true)==6,
        "reported 24p clip: 720x480i NTSC at 15 kHz");
    check(selectMode(1920,1034,24,1,defaults_modelines,false)==8,
        "reported 24p clip: 720x480p NTSC when unrestricted");
    const unsigned rates[][2]={{24,1},{24000,1001},{25,1},{50,1},{30,1},{30000,1001},{60,1},{60000,1001}};
    const unsigned sizes[][2]={{320,240},{640,480},{720,576},{1920,1034},{1920,1080},{3840,2160}};
    for (const auto& rate:rates) for (const auto& size:sizes) for (bool limit:{false,true}) {
        const int selected=selectMode(size[0],size[1],rate[0],rate[1],defaults_modelines,limit);
        check(selected>=2,"real automatic preset exists");
        const auto& mode=defaults_modelines[selected];
        const double horizontal=mode.pClock*1000000/mode.hTotal;
        const double refresh=horizontal/mode.vTotal*(mode.interlace?2:1);
        const bool pal=rate[0]==25 || rate[0]==50;
        check(std::abs(refresh-(pal?50:60))<0.2,"toggle preserves 50/60 family with known source FPS");
        check(!limit || (horizontal>=15000 && horizontal<=16500),"real preset obeys 15k limit");
    }
    // Removing the limit is permission, not a request to force a 31 kHz mode.
    const int unrestricted=selectMode(320,240,24,1,defaults_modelines,false);
    check(defaults_modelines[unrestricted].pClock*1000000/defaults_modelines[unrestricted].hTotal<16500,
        "unrestricted can keep a suitable 15k mode");

    AutomaticModeInputs applied; applied.updateRate(24,1);
    ModeState<modeline_struct> state;
    unsigned switches=0;
    auto apply=[&](const modeline_struct&){ ++switches; return true; };
    check(state.apply(defaults_modelines[6],apply),"initial automatic 15k hardware mode");
    for (unsigned i=0;i<100;++i) {
        auto requested=applied; requested.only15k=!applied.only15k;
        check(requested.requiresReselect(1,applied),"saving only15k alone refreshes Automatic");
        check(!requested.requiresReselect(0,applied) && !requested.requiresReselect(6,applied),
            "only15k does not change Manual or fixed presets");
        const int mode=selectMode(1920,1034,requested.rate,requested.base,defaults_modelines,requested.only15k);
        check(state.apply(defaults_modelines[mode],apply),"apply automatic limit toggle");
        applied=requested;
        check(!requested.requiresReselect(1,applied),"unchanged config does not repeat mode requests");
    }
    check(switches==101,"one mode switch per effective automatic toggle");
    AutomaticModeInputs unknown, detected;
    check(selectMode(1920,1034,unknown.rate,unknown.base,defaults_modelines,true)==13,
        "unknown FPS reproduces dimension-only 576i PAL selection");
    detected.updateRate(24,1);
    check(detected.requiresReselect(1,unknown),"late source FPS corrects Automatic without preference changes");
    unknown=detected; detected.updateRate(0,0);
    check(!detected.requiresReselect(1,unknown),"missing metadata does not erase known FPS");
    detected.updateRate(24000,1000);
    check(!detected.requiresReselect(1,unknown),"equivalent rational FPS does not reconfigure");
    detected.updateRate(25,1);
    check(detected.requiresReselect(1,unknown),"actual FPS change refreshes Automatic");
}

static void queueTimelineTests()
{
    using namespace video_stream;
    FrameQueue queue;
    check(queue.allocate(),"timeline queue allocation");
    auto put=[&](int64_t pts) {
        const int slot=queue.reserve(); check(slot>=0,"timeline reserve");
        queue.slots[slot].pts=pts; check(queue.submit(slot),"timeline submit");
        return slot;
    };
    const int held=put(1000);
    check(queue.takeDue(1000)==held,"initial displayed picture");
    put(2000);
    const int fresh=put(300000);
    check(queue.takeDue(300000)==fresh && queue.dropped_expired==1,
        "stall drops expired pending picture and selects current one");
    check(queue.slots[held].owner==Ownership::sending,"expiry never frees displayed picture");
    queue.release(fresh);
    put(300001);
    check(queue.takeDue(600002)<0 && queue.dropped_expired==2,
        "all expired pending pictures are dropped, not replayed");
    check(queue.slots[held].owner==Ownership::sending,"pause retains last displayed image");
    const int future=put(900000);
    check(queue.takeDue(700000)<0 && queue.slots[future].owner==Ownership::ready,
        "expiry preserves future presentation dates");
    const int pending=put(950000);
    const int writing=queue.reserve(); check(writing>=0,"in-progress conversion");
    const int reset=put(800000);
    check(queue.pts_regressions==1 && queue.dropped_discontinuity==2,
        "backwards date drops old pending timeline immediately");
    check(queue.slots[future].owner==Ownership::free && queue.slots[pending].owner==Ownership::free,
        "old future frames cannot reappear after date regression");
    check(queue.slots[held].owner==Ownership::sending && queue.slots[writing].owner==Ownership::writing,
        "date regression preserves sender and converter ownership");
    check(queue.takeDue(799999)<0 && queue.takeDue(800000)==reset,"new timeline waits until due");
    queue.release(reset); queue.release(writing); queue.release(held);
    const int first=put(800001), replacement=put(800001);
    check(queue.takeDue(800001)==replacement && queue.slots[first].owner==Ownership::free,
        "duplicate date uses newest content");
    check(queue.pts_regressions==1,"equal dates are not discontinuities");
    queue.release(replacement);
    const int boundary=put(800002);
    check(queue.takeDue(1050002)==boundary,"250 ms inclusive deadline"); queue.release(boundary);
    const int obsolete=queue.reserve(); check(obsolete>=0,"old mode writer");
    queue.slots[obsolete].pts=1;
    queue.changeGeneration();
    check(!queue.submit(obsolete),"old generation rejected before timeline tracking");
    const int newMode=put(1);
    check(queue.pts_regressions==1 && queue.takeDue(1)==newMode,"new mode starts a fresh timeline");
    queue.release(newMode);
    queue.clear(); check(queue.dropped_expired==0 && queue.pts_regressions==0,"close resets timeline metrics");

    // Repeat/drop cadence at rational rates, without accumulating floating-point
    // timing error. Dates are in the monotonic presentation clock domain.
    const unsigned cadences[][2]={{24000,1001},{30000,1001},{25,1},{50,1}};
    for (const auto& rate:cadences) {
        check(queue.allocate(),"cadence queue");
        const unsigned outRate=(rate[0]==25 || rate[0]==50)?50:60000;
        const unsigned outBase=outRate==50?1:1001;
        unsigned source=0, selected=0, repeated=0;
        int current=-1;
        for (unsigned tick=0;tick<600;++tick) {
            const int64_t now=1+int64_t(tick)*1000000*outBase/outRate;
            while (1+int64_t(source)*1000000*rate[1]/rate[0]<=now) {
                put(1+int64_t(source)*1000000*rate[1]/rate[0]); ++source;
            }
            const int due=queue.takeDue(now);
            if (due>=0) { queue.release(current); current=due; ++selected; }
            else ++repeated;
            check(current>=0 && queue.slots[current].pts<=now,"rational cadence never presents early");
        }
        check(selected==source && selected+repeated==600 && queue.dropped_expired==0 && queue.pts_regressions==0,
            "rational cadence retains source pictures and repeats at output rate");
        queue.release(current);
    }
    TimingStats timing;
    check(timing.average_us()==0,"empty timing average");
    timing.add(100,110); timing.add(100,130); timing.add(200,199);
    check(timing.count==2 && timing.average_us()==20 && timing.max_us==30,"timing aggregates and invalid interval");
    timing.add((std::numeric_limits<int64_t>::min)(),(std::numeric_limits<int64_t>::max)());
    check(timing.count==2,"timing total overflow rejected");
}

static void pacingAndReductionTests()
{
    using namespace video_stream;
    FrameQueue queue;
    check(queue.allocate(), "timestamp queue");
    for (int i=0; i<4; ++i) {
        const int slot=queue.reserve(); queue.slots[slot].pts=1000+i*20000;
        check(queue.submit(slot), "timed submit");
    }
    check(queue.takeDue(999)<0, "future frames are held");
    int selected=queue.takeDue(45000);
    check(selected>=0 && queue.slots[selected].pts==41000 && queue.dropped_late==2,
        "stall skips obsolete frames and selects newest due PTS");
    queue.release(selected);
    check(queue.takeDue(60000)<0, "next future frame not early");
    selected=queue.takeDue(61000); check(selected>=0, "future frame becomes due"); queue.release(selected);

    struct Mode { double pClock; unsigned hActive,hTotal,vActive,vTotal,interlace; };
    const Mode modes[]={{}, {}, {6.7,320,426,240,262,0}, {6.66,320,426,240,312,0},
        {27,720,864,576,625,0}};
    check(selectMode(320,240,25,1,modes,true)==3, "25 fps selects PAL");
    check(selectMode(320,240,50,1,modes,true)==3, "50 fps selects PAL");
    check(selectMode(320,240,30000,1001,modes,true)==2, "29.97 selects NTSC");
    check(selectMode(320,240,24000,1001,modes,true)==2, "23.976 selects NTSC cadence");
    check(selectMode(720,576,50,1,modes,false)==4, "last array preset considered");
    check(selectMode(720,576,50,1,modes,true)==3, "15k filter uses horizontal frequency");

    uint8_t y[16], uv[4]={128,128,128,128};
    for (unsigned i=0;i<16;++i) y[i]=((i%4+i/4)&1)?255:0;
    I420View src; src.width=src.height=src.coded_width=src.coded_height=4;
    src.full_range=true;
    src.planes[0]={y,4,4}; src.planes[1]=src.planes[2]={uv,2,2};
    uint8_t out[14]; std::memset(out,0xA5,sizeof(out));
    check(convert(src,out+1,12,2,2,false,true), "area downscale");
    for (unsigned i=1;i<13;++i) check(out[i]==128, "checkerboard averages instead of aliasing");
    check(out[0]==0xA5 && out[13]==0xA5, "filtered output canaries");
    check(convert(src,out+1,12,2,2,false,false), "nearest option");
    check(out[1]==0, "nearest option retains original sampling");
}

static void fieldTests()
{
    using namespace video_stream;
    std::array<uint8_t, 36> image;
    for (size_t row = 0; row < 4; ++row)
        for (size_t x = 0; x < 9; ++x) image[row * 9 + x] = uint8_t(row + 1);
    std::array<uint8_t, 20> field;
    field.fill(0xA5);
    check(copyForSend(field.data() + 1, 18, image.data(), image.size(), 3, 4, true, 0), "even field");
    check(field[1] == 1 && field[10] == 3, "even field rows");
    check(copyForSend(field.data() + 1, 18, image.data(), image.size(), 3, 4, true, 1), "odd field");
    check(field[1] == 2 && field[10] == 4, "odd field rows");
    check(field.front() == 0xA5 && field.back() == 0xA5, "field canaries");
    check(!copyForSend(field.data(), 17, image.data(), image.size(), 3, 4, true, 0), "short send buffer");
    check(!copyForSend(field.data(), 20, image.data(), 18, 3, 4, true, 0), "old frame dimensions");
    size_t bytes = 0;
    check(!frameSize(720, 575, true, bytes), "odd interlaced height");
    check(!frameSize(65535, 65535, false, bytes), "capacity and overflow");
    check(frameSize(720, 576, true, bytes) && bytes == 1244160, "PAL size");
}

static void queueTests()
{
    using namespace video_stream;
    FrameQueue queue;
    check(queue.allocate(), "pool allocation");
    std::array<const uint8_t*, FrameQueue::slot_count> storage;
    for (unsigned i = 0; i < FrameQueue::slot_count; ++i) storage[i] = queue.slots[i].pixels.get();
    const int first = queue.reserve();
    queue.slots[first].pixels[0] = 42;
    check(queue.submit(first) && queue.take() == first, "first frame must not be skipped");
    for (unsigned i = 0; i < 8; ++i) {
        const int writer = queue.reserve();
        check(writer >= 0 && writer != first, "never overwrite current sender");
        queue.slots[writer].pts = i;
        check(queue.submit(writer), "saturated submit");
    }
    check(queue.pending() == 4 && queue.dropped_full == 4, "drop oldest pending when full");
    check(queue.slots[first].pixels[0] == 42, "sender survives queue pressure");
    const int pending = queue.take();
    check(queue.slots[pending].pts == 4, "keep recent pending frames in FIFO order");
    queue.release(pending);
    const int writing = queue.reserve();
    const uint64_t old_generation = queue.slots[writing].generation;
    queue.changeGeneration();
    check(queue.pending() == 0 && queue.take() < 0, "mode change clears pending frames");
    check(queue.slots[first].owner == Ownership::sending && queue.slots[first].pixels[0] == 42,
        "mode change retains in-flight sender storage");
    check(queue.slots[writing].owner == Ownership::writing && queue.generation != old_generation,
        "mode change retains in-flight conversion");
    check(!queue.submit(writing), "reject late conversion from old mode");
    queue.release(first);
    for (unsigned i = 0; i < 10000; ++i) {
        const int slot = queue.reserve();
        check(slot >= 0 && queue.submit(slot), "steady-state queue");
        queue.release(queue.take());
    }
    for (unsigned i = 0; i < FrameQueue::slot_count; ++i)
        check(storage[i] == queue.slots[i].pixels.get(), "no buffer reallocations across frames/modes");

    struct Mode { double pClock; unsigned hActive, hBegin, hEnd, hTotal, vActive, vBegin, vEnd, vTotal, interlace; };
    Mode mode{ 13.875, 720, 741, 806, 888, 576, 581, 586, 625, 1 };
    check(validMode(mode), "valid PAL mode");
    check(sameMode(mode, mode), "identical effective mode needs no switch");
    Mode changed = mode; changed.pClock += 0.001;
    check(!sameMode(mode, changed), "clock-only mode change detected");
    mode.vActive = 575; check(!validMode(mode), "reject odd field height"); mode.vActive = 576;
    mode.hEnd = 900; check(!validMode(mode), "reject reversed timing"); mode.hEnd = 806;
    mode.pClock = 0; check(!validMode(mode), "reject zero clock");
}

static void concurrentQueueTests()
{
    using namespace video_stream;
    FrameQueue queue;
    check(queue.allocate(), "concurrent allocation");
    std::mutex mutex;
    std::atomic<bool> done{ false };
    std::thread consumer([&] {
        for (;;) {
            int slot;
            {
                std::lock_guard<std::mutex> lock(mutex);
                slot = queue.take();
                if (slot < 0 && done) break;
            }
            if (slot < 0) { std::this_thread::yield(); continue; }
            const auto& frame = queue.slots[slot];
            const uint8_t marker = uint8_t(frame.pts);
            std::this_thread::yield();
            for (unsigned j = 0; j < 64; ++j)
                check(frame.pixels[j] == marker, "in-flight pixels immutable under concurrent mode changes");
            std::lock_guard<std::mutex> lock(mutex);
            queue.release(slot);
        }
    });
    std::thread changer([&] {
        for (int i = 0; i < 1000; ++i) {
            { std::lock_guard<std::mutex> lock(mutex); queue.changeGeneration(); }
            std::this_thread::yield();
        }
    });
    for (int i = 0; i < 10000; ++i) {
        int slot;
        { std::lock_guard<std::mutex> lock(mutex); slot = queue.reserve(); }
        check(slot >= 0, "producer has spare slot");
        queue.slots[slot].pts = i;
        std::memset(queue.slots[slot].pixels.get(), uint8_t(i), 64);
        { std::lock_guard<std::mutex> lock(mutex); queue.submit(slot); }
    }
    changer.join();
    done = true;
    consumer.join();
    check(queue.pending() == 0, "concurrent queue drained");
}

static void resolutionTransitionTests()
{
    using namespace video_stream;
    FrameQueue queue;
    check(queue.allocate(), "mode-transition buffers");
    const std::array<uint8_t, 4> y{ 235, 235, 235, 235 };
    const uint8_t uv = 128;
    I420View source;
    source.width = source.height = source.coded_width = source.coded_height = 2;
    source.planes[0] = { y.data(), 2, 2 };
    source.planes[1] = source.planes[2] = { &uv, 1, 1 };
    const int small = queue.reserve();
    Frame& a = queue.slots[small];
    a.width = 3; a.height = 4; a.interlace = false;
    check(frameSize(a.width, a.height, false, a.bytes), "small frame metadata");
    check(convert(source, a.pixels.get(), frame_capacity, a.width, a.height, false), "small conversion");
    check(queue.submit(small) && queue.take() == small, "small frame in sender");
    queue.changeGeneration();
    const int large = queue.reserve();
    check(large != small, "resize does not reclaim frame being sent");
    Frame& b = queue.slots[large];
    b.width = 720; b.height = 576; b.interlace = true;
    check(frameSize(b.width, b.height, true, b.bytes), "large metadata");
    check(convert(source, b.pixels.get(), frame_capacity, b.width, b.height, false), "large conversion");
    check(queue.submit(large), "large frame submitted");
    std::vector<uint8_t> sent(api_capacity);
    check(!copyForSend(sent.data(), sent.size(), a.pixels.get(), a.bytes, b.width, b.height, true, 0),
        "never send old allocation using new dimensions");
    queue.release(small);
    check(queue.take() == large, "new mode frame selected");
    check(copyForSend(sent.data(), sent.size(), b.pixels.get(), b.bytes, b.width, b.height, true, 0),
        "first field after resize");
    check(copyForSend(sent.data(), sent.size(), b.pixels.get(), b.bytes, b.width, b.height, true, 1),
        "reuse full frame for next field");
    check(sent[0] == 255 && sent[b.bytes / 2 - 1] == 255, "large field completely populated");
    queue.changeGeneration(); queue.release(large);
    check(queue.pending() == 0, "large-to-small transition flushes previous mode");
}

int main()
{
    check(audio_stream::readableSamples(1000000000, 960, 288000, 2) == 960,
        "audio PTS jump cannot drain beyond available samples");
    check(audio_stream::readableSamples(-1, 960, 288000, 2) == 0, "negative audio request");
    check(audio_stream::readableSamples(100, -1, 288000, 2) == 0, "invalid audio occupancy");
    check(audio_stream::readableSamples(999, 960, 288000, 2) == 960, "audio shortage clamps");
    check(audio_stream::readableSamples(959, 960, 288000, 2) == 958, "whole audio channel groups");
    check(audio_stream::readableSamples(INT64_MAX, 300000, 288000, 2) == 288000,
        "audio capacity limit");
    check(audio_stream::readableSamples(100, 960, 288000, 0) == 0, "closed audio channels");
    conversionTests();
    pacingAndReductionTests();
    automaticModeTests();
    queueTimelineTests();
    aspectTests();
    modeApplicationTests();
    fieldTests();
    queueTests();
    concurrentQueueTests();
    resolutionTransitionTests();
    std::puts("PASS: conversion, color, pitch, crop, boundaries, fields, modes, pool, concurrent ownership");
}
