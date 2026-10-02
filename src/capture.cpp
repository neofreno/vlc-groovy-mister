#include "capture.h"
#include "video_frame.h"
#include "video_queue.h"
#include "video_metrics.h"
#include "stream_startup.h"
#include "stream_health.h"
#include "video_ack_metrics.h"
#include <condition_variable>
#include <mutex>
#include <string>

static std::mutex videoMutex;
static std::condition_variable videoReady;
static video_stream::FrameQueue frames;
static modeline_struct requestedMode{};
static bool requestedAspect = false;
static bool requestedSmooth = true;
static vlc_object_t* t_object = nullptr;
static std::string misterHost;
static uint8_t lz4Frames = 0;
static video_stream::AudioFormat negotiatedAudio{48000, 2}; // Worker-owned.
static uint16_t mtu = 1500;
static double frate = 25;
static vlc_thread_t worker;
static bool threadStarted = false;
static bool running = false; // Protected by videoMutex.
static std::atomic<bool> m_initialized{false};

struct StreamStats {
    uint64_t received = 0, queued = 0, rejected = 0;
    uint64_t submitted = 0, repeated = 0, send_errors = 0;
    uint64_t submitted_progressive = 0, submitted_fields = 0;
    uint64_t init_attempts = 0, init_failures = 0;
    uint64_t reconnects = 0;
    uint64_t audio_reconfigurations = 0;
    video_stream::TimingStats conversion, send, sync_wait, first_submit_age;
    video_stream::AckMetrics acknowledgements;
};
static StreamStats stats;

static void* captureloop(void*);
static bool initializeApi();
static bool switchMode(const modeline_struct& mode);

static void reportMetrics(const char* reason)
{
    StreamStats snapshot;
    unsigned pending, peak;
    uint64_t dropped;
    {
        std::lock_guard<std::mutex> lock(videoMutex);
        snapshot = stats;
        pending = frames.pending(); peak = frames.high_water;
        dropped = frames.dropped_full + frames.dropped_mode + frames.dropped_late +
            frames.dropped_expired + frames.dropped_discontinuity;
    }
    // Do not hold the producer's queue mutex while VLC writes the log.
    const auto& ack = snapshot.acknowledgements;
    msg_Dbg(t_object, "stream metrics %s (cumulative): submitted=%llu ack_observed=%llu "
        "ack_unobserved=%llu ack_overwritten=%llu ack_abandoned=%llu ack_pending=%u "
        "ack_progressive=%llu ack_fields=%llu last_echo=%u last_sequence=%llu last_field=%u "
        "queue=%u/%u dropped=%llu reconnects=%llu audio_reconfigurations=%llu",
        reason, snapshot.submitted, ack.matched, ack.unobserved, ack.overwritten, ack.abandoned, ack.pending(),
        ack.progressive, ack.fields, ack.last_frame, ack.last_sequence, ack.last_field,
        pending, peak, dropped, snapshot.reconnects, snapshot.audio_reconfigurations);
    msg_Dbg(t_object, "stream timings %s (cumulative us average/max): conversion=%llu/%llu "
        "send=%llu/%llu WaitSync=%llu/%llu first_submit_age=%llu/%llu ack_observation=%llu/%llu "
        "(ACK observation includes polling/scheduling delay; not CRT latency)", reason,
        snapshot.conversion.average_us(), snapshot.conversion.max_us,
        snapshot.send.average_us(), snapshot.send.max_us,
        snapshot.sync_wait.average_us(), snapshot.sync_wait.max_us,
        snapshot.first_submit_age.average_us(), snapshot.first_submit_age.max_us,
        ack.observation_delay.average_us(), ack.observation_delay.max_us);
}

static void observeAck(uint32_t echo, int64_t now)
{
    std::lock_guard<std::mutex> lock(videoMutex);
    stats.acknowledgements.observe(echo, now);
}

bool capture::groovy_inicialiced()
{
    return m_initialized.load();
}

bool capture::validModeline(const modeline_struct& mode)
{
    return video_stream::validMode(mode);
}

bool capture::init_class(modeline_struct* mode, vlc_object_t* object,
    bool aspectratio, bool smooth, const char* host, uint8_t compress,
    uint8_t rgbMode, uint16_t mtus, double videoFps)
{
    if (threadStarted) return true;
    if (!mode || !validModeline(*mode) || rgbMode != RGB_888) {
        msg_Err(object, "Invalid video mode or unsupported output pixel format");
        return false;
    }
    std::lock_guard<std::mutex> lock(videoMutex);
    if (!frames.allocate()) {
        msg_Err(object, "Cannot allocate reusable video buffers");
        return false;
    }
    requestedMode = *mode;
    requestedAspect = aspectratio;
    requestedSmooth = smooth;
    t_object = object;
    misterHost = host ? host : "";
    lz4Frames = compress;
    negotiatedAudio = {48000, 2};
    mtu = mtus;
    frate = videoFps > 0 ? videoFps : 25;
    stats = {};
    m_initialized = false;
    running = true;
    joypad::open(object); // Controls failure must not prevent audio/video.
    if (vlc_clone(&worker, captureloop, nullptr, VLC_THREAD_PRIORITY_LOW)) {
        joypad::close();
        running = false;
        frames.clear();
        msg_Err(object, "Cannot create video sender thread");
        return false;
    }
    threadStarted = true;
    msg_Dbg(object, "Video sender started: %u reusable buffers, converter=exact-tiled-v1", video_stream::FrameQueue::slot_count);
    return true;
}

static bool makeView(const picture_t* picture, video_stream::I420View& view)
{
    if (!picture || picture->i_planes != 3 ||
        (picture->format.i_chroma != VLC_CODEC_I420 && picture->format.i_chroma != VLC_CODEC_J420))
        return false;
    const video_format_t& fmt = picture->format;
    if (fmt.orientation != ORIENT_NORMAL) return false;
    view.coded_width = fmt.i_width;
    view.coded_height = fmt.i_height;
    view.x = fmt.i_x_offset;
    view.y = fmt.i_y_offset;
    view.width = fmt.i_visible_width;
    view.height = fmt.i_visible_height;
    view.full_range = fmt.b_color_range_full || fmt.i_chroma == VLC_CODEC_J420;
    view.matrix = fmt.space == COLOR_SPACE_BT2020 ? video_stream::Matrix::bt2020 :
        fmt.space == COLOR_SPACE_BT709 ? video_stream::Matrix::bt709 :
        fmt.space == COLOR_SPACE_BT601 ? video_stream::Matrix::bt601 :
        fmt.i_visible_height > 576 ? video_stream::Matrix::bt709 : video_stream::Matrix::bt601;
    for (int p = 0; p < 3; ++p)
        view.planes[p] = { picture->p[p].p_pixels, picture->p[p].i_pitch, picture->p[p].i_lines };
    return video_stream::valid(view);
}

bool capture::storeFrame(const picture_t* picture, unsigned sarNum, unsigned sarDen)
{
    video_stream::I420View view;
    const bool valid = makeView(picture, view);
    view.sar_num = sarNum;
    view.sar_den = sarDen;
    int index;
    bool aspect, smooth;
    {
        std::lock_guard<std::mutex> lock(videoMutex);
        ++stats.received;
        if (!running || !m_initialized || !valid) { ++stats.rejected; return false; }
        index = frames.reserve();
        if (index < 0) { ++stats.rejected; return false; }
        video_stream::Frame& frame = frames.slots[index];
        frame.width = requestedMode.hActive;
        frame.height = requestedMode.vActive;
        frame.interlace = requestedMode.interlace != 0;
        video_stream::frameSize(frame.width, frame.height, frame.interlace, frame.bytes);
        frame.pts = picture->date > VLC_TS_INVALID ? picture->date : mdate();
        aspect = requestedAspect;
        smooth = requestedSmooth;
    }
    // This slot belongs exclusively to the producer until submit/release.
    video_stream::Frame& frame = frames.slots[index];
    const int64_t conversionStart = mdate();
    const bool converted = video_stream::convert(view, frame.pixels.get(),
        video_stream::frame_capacity, frame.width, frame.height, aspect, smooth);
    const int64_t conversionEnd = mdate();
    bool accepted;
    {
        std::lock_guard<std::mutex> lock(videoMutex);
        stats.conversion.add(conversionStart, conversionEnd);
        accepted = running && converted;
        if (accepted) accepted = frames.submit(index);
        else frames.release(index);
        if (accepted) ++stats.queued;
        else ++stats.rejected;
    }
    videoReady.notify_one();
    return accepted;
}

bool capture::changeModeline(const modeline_struct* mode, bool aspect, bool smooth)
{
    if (!mode || !validModeline(*mode)) return false;
    {
        std::lock_guard<std::mutex> lock(videoMutex);
        if (!running) return false;
        if (video_stream::sameMode(requestedMode, *mode) && requestedAspect == aspect && requestedSmooth == smooth) return true;
        requestedMode = *mode;
        requestedAspect = aspect;
        requestedSmooth = smooth;
        frames.changeGeneration();
    }
    videoReady.notify_one();
    return true;
}

static bool sendFrame(const video_stream::Frame& frame, uint32_t number, unsigned field)
{
#if !DEBUG_GROOVY
    uint8_t* dst = reinterpret_cast<uint8_t*>(gmw_get_pBufferBlit(uint8_t(field)));
    if (!video_stream::copyForSend(dst, video_stream::api_capacity, frame.pixels.get(),
            frame.bytes, frame.width, frame.height, frame.interlace, field))
        return false;
    gmw_blit(number, uint8_t(field), 0, 15000, 0);
#endif
    // CmdBlit is void: success here means submission, NOT FPGA acknowledgement.
    return true;
}

static void* captureloop(void*)
{
    if (!initializeApi()) {
        std::lock_guard<std::mutex> lock(videoMutex);
        running = false;
        return nullptr;
    }
    video_stream::ModeState<modeline_struct> hardwareMode;
    unsigned modeFailures = 0;
    uint64_t appliedGeneration = 0;
    uint32_t frameNumber = 0;
    int current = -1;
    uint64_t lastSubmittedSequence = 0;
    video_stream::AckWatchdog watchdog;
    video_stream::PeriodicMetrics reporting(mdate());
    for (;;) {
        if (reporting.due(mdate())) reportMetrics("periodic");
        int index = -1;
        modeline_struct mode{};
        uint64_t generation = 0;
        bool change = false;
        {
            std::unique_lock<std::mutex> lock(videoMutex);
            // Also inspect late audio when no new video is being published.
            videoReady.wait_for(lock, std::chrono::milliseconds(50), [&] {
                return !running || appliedGeneration != frames.generation ||
                    current >= 0 || frames.pending() != 0;
            });
            if (!running) break;
            generation = frames.generation;
            change = appliedGeneration != generation;
            if (change) {
                frames.release(current);
                current = -1;
                mode = requestedMode;
            } else {
                index = frames.takeDue(mdate());
                if (index >= 0) {
                    frames.release(current);
                    current = index;
                }
            }
        }
        gmw_fpgaStatus status{};
        const bool audioChanged = video_stream::needsAudioInit(captureAudio::format(), negotiatedAudio);
        bool lost = false;
#if !DEBUG_GROOVY
        gmw_getStatus(&status);
        lost = watchdog.expired(mdate(), status.frameEcho);
        if (lost) {
            // Normally WaitSync consumes ACKs for raster correction. Only
            // drain here at timeout, so a just-arrived ACK avoids a false loss
            // without stealing timing feedback from the normal sync path.
            gmw_getACK(0);
            gmw_getStatus(&status);
            lost = watchdog.expired(mdate(), status.frameEcho);
        }
        observeAck(status.frameEcho, mdate());
#endif
        if (lost || audioChanged) {
            joypad::invalidate();
            // Gate producers first; then discard queued data from the old
            // session, without reclaiming a slot still being converted.
            m_initialized = false;
            {
                std::lock_guard<std::mutex> lock(videoMutex);
                if (lost) ++stats.reconnects;
                if (audioChanged) ++stats.audio_reconfigurations;
                stats.acknowledgements.newSession();
                frames.release(current);
                current = -1;
                frames.changeGeneration();
            }
            captureAudio::flush_audio();
            if (lost)
                msg_Warn(t_object, "No advancing video ACK for 1000 ms; stopping media and opening a new session");
            else
                msg_Dbg(t_object, "Audio format changed; renegotiating INIT and reapplying video mode before media");
            hardwareMode = {};
            appliedGeneration = 0;
            frameNumber = 0;
            lastSubmittedSequence = 0;
            modeFailures = 0;
            watchdog.reset();
            if (!initializeApi()) break;
            continue; // Reapply even identical timings before accepting media.
        }
        if (change) {
            if (!hardwareMode.apply(mode, switchMode)) {
                m_initialized = false;
                if (++modeFailures == 1 || modeFailures % 10 == 0)
                    msg_Err(t_object, "Video mode failed (attempt %u); retrying, sender remains responsive",
                        modeFailures);
                std::unique_lock<std::mutex> lock(videoMutex);
                videoReady.wait_for(lock, std::chrono::milliseconds(500), [&] {
                    return !running || frames.generation != generation;
                });
                continue;
            }
            modeFailures = 0;
            appliedGeneration = generation;
            m_initialized = true;
            continue; // Check for a newer mode request before selecting a frame.
        }
        // Poll even without a new picture (pause, seek or a future-dated frame).
#if !DEBUG_GROOVY
        joypad::getbuttonjoypad();
#endif
        if (current < 0) {
            // Forced/early display callbacks can contain a future date.
            std::unique_lock<std::mutex> lock(videoMutex);
            videoReady.wait_for(lock, std::chrono::milliseconds(2));
            continue;
        }

        const video_stream::Frame& frame = frames.slots[current];
        const unsigned field = frame.interlace
            ? unsigned((!status.vgaF1) ^ ((frameNumber - status.frame) & 1u)) : 0u;
        if (!frame.interlace || field == 0)
            captureAudio::add_audio_to_recording(frame.pts, int(field), frate, negotiatedAudio);
        ++frameNumber;
        if (status.frame > frameNumber) frameNumber = status.frame + 1;
        const int64_t sendStart = mdate();
        const bool submitted = sendFrame(frame, frameNumber, field);
        const int64_t sendEnd = mdate();
        if (submitted) watchdog.sent(sendEnd, status.frameEcho);
        {
            std::lock_guard<std::mutex> lock(videoMutex);
            stats.send.add(sendStart, sendEnd);
            if (submitted) {
                ++stats.submitted;
#if !DEBUG_GROOVY
                stats.acknowledgements.sent(frameNumber, frame.sequence, sendStart, frame.interlace, field);
#endif
                if (frame.interlace) ++stats.submitted_fields;
                else ++stats.submitted_progressive;
                if (frame.sequence == lastSubmittedSequence) ++stats.repeated;
                else stats.first_submit_age.add(frame.pts, sendStart);
                lastSubmittedSequence = frame.sequence;
            } else ++stats.send_errors;
        }
        const int64_t syncStart = mdate();
#if !DEBUG_GROOVY
        gmw_waitSync();
#else
        msleep(20000);
#endif
        const int64_t syncEnd = mdate();
#if !DEBUG_GROOVY
        // Copy already-consumed status; do not take ACKs away from WaitSync.
        gmw_fpgaStatus observed{};
        gmw_getStatus(&observed);
        observeAck(observed.frameEcho, syncEnd);
#endif
        {
            std::lock_guard<std::mutex> lock(videoMutex);
            stats.sync_wait.add(syncStart, syncEnd);
        }
    }
    {
        std::lock_guard<std::mutex> lock(videoMutex);
        frames.release(current);
    }
    m_initialized = false;
    return nullptr;
}

static bool initializeApi()
{
#if !DEBUG_GROOVY
    const bool initialized = video_stream::initializeUntilStopped(
        [] {
            std::lock_guard<std::mutex> lock(videoMutex);
            return running;
        },
        [] {
            // Do not hold videoMutex while waiting for the network handshake.
            {
                std::lock_guard<std::mutex> lock(videoMutex);
                ++stats.init_attempts;
            }
            const auto desired = video_stream::selectAudioFormat(captureAudio::format(), negotiatedAudio);
            const bool ok = gmw_init(misterHost.c_str(), lz4Frames, desired.rate, desired.channels, RGB_888, mtu) == 0;
            if (ok) negotiatedAudio = desired;
            return ok;
        },
        [](unsigned delayMs) {
            std::unique_lock<std::mutex> lock(videoMutex);
            return !videoReady.wait_for(lock, std::chrono::milliseconds(delayMs), [] { return !running; });
        },
        [](const video_stream::StartupRetry& retry) {
            {
                std::lock_guard<std::mutex> lock(videoMutex);
                ++stats.init_failures;
            }
            if (retry.shouldLog())
                msg_Warn(t_object, "Groovy MiSTer init failed (attempt %llu); retrying in %u ms. Stop playback to cancel",
                    retry.failures, retry.delay_ms);
        });
    if (!initialized) return false;
    msg_Dbg(t_object, "Groovy MiSTer API initialized after %llu attempt(s), audio=%u Hz/%u channels (video does not wait for audio)",
        stats.init_attempts, negotiatedAudio.rate, unsigned(negotiatedAudio.channels));
    joypad::init(misterHost.c_str());
    return true;
#else
    negotiatedAudio = video_stream::selectAudioFormat(captureAudio::format(), negotiatedAudio);
    return true;
#endif
}

static bool switchMode(const modeline_struct& mode)
{
#if !DEBUG_GROOVY
    gmw_switchres(mode.pClock, mode.hActive, mode.hBegin, mode.hEnd, mode.hTotal,
        mode.vActive, mode.vBegin, mode.vEnd, mode.vTotal, mode.interlace);
    // The linked API clears its RGB size on invalid mode / control-ACK failure.
    if (!gmw_get_pBufferBlit(0)) return false;
#endif
    msg_Dbg(t_object, "Stream mode applied: %ux%u interlace=%u",
        mode.hActive, mode.vActive, mode.interlace);
    return true;
}

bool capture::close()
{
    if (!threadStarted) return true;
    joypad::stop(); // No controls can start while a network handshake is finishing.
    {
        std::lock_guard<std::mutex> lock(videoMutex);
        running = false;
    }
    videoReady.notify_all();
    vlc_join(worker, nullptr);
    joypad::close();
    threadStarted = false;
    m_initialized = false;
    reportMetrics("final");
    {
        std::lock_guard<std::mutex> lock(videoMutex);
        msg_Dbg(t_object, "video startup summary: init_attempts=%llu init_failures=%llu reconnects=%llu audio_reconfigurations=%llu",
            stats.init_attempts, stats.init_failures, stats.reconnects, stats.audio_reconfigurations);
        msg_Dbg(t_object, "video sender summary: received=%llu queued=%llu rejected=%llu "
            "dropped_full=%llu dropped_mode=%llu submitted=%llu repeated=%llu send_errors=%llu queue_peak=%u dropped_late=%llu "
            "dropped_expired=%llu dropped_discontinuity=%llu pts_regressions=%llu progressive_submitted=%llu fields_submitted=%llu",
            stats.received, stats.queued, stats.rejected, frames.dropped_full, frames.dropped_mode,
            stats.submitted, stats.repeated, stats.send_errors, frames.high_water, frames.dropped_late,
            frames.dropped_expired, frames.dropped_discontinuity, frames.pts_regressions,
            stats.submitted_progressive, stats.submitted_fields);
        msg_Dbg(t_object, "video timings us (average/max): conversion=%llu/%llu send=%llu/%llu "
            "sync_wait=%llu/%llu first_submit_age=%llu/%llu age_samples=%llu",
            stats.conversion.average_us(), stats.conversion.max_us,
            stats.send.average_us(), stats.send.max_us,
            stats.sync_wait.average_us(), stats.sync_wait.max_us,
            stats.first_submit_age.average_us(), stats.first_submit_age.max_us,
            stats.first_submit_age.count);
        frames.clear();
    }
#if !DEBUG_GROOVY
    gmw_close();
#endif
    t_object = nullptr;
    misterHost.clear();
    return true;
}
