#include "msvc-compat/poll.h"
#include "msvc-compat/types.h"
#include "capture.h"
#include "video_mode.h"
#include "CaptureAudio.h"

#include <vlc_configuration.h>  // Para config_AddCallback()


#ifdef HAVE_CONFIG_H
# include "config.h"
#else
# define N_(str) (str)
#endif

#include <libvlc_version.h>
#include "version.h"

#if LIBVLC_VERSION_MAJOR >= 3
# define VLC_MODULE_LICENSE VLC_LICENSE_LGPL_2_1_PLUS
# define VLC_MODULE_COPYRIGHT VERSION_COPYRIGHT
#endif


#include <vlc_plugin.h>
#include <vlc_threads.h>

#if LIBVLC_VERSION_MAJOR == 2 && LIBVLC_VERSION_MINOR == 1
# include "third_party/vlc/2.1.0/include/vlc_interface.h"
#elif LIBVLC_VERSION_MAJOR == 2 && LIBVLC_VERSION_MINOR == 2
# include "third_party/vlc/2.2.0/include/vlc_interface.h"
#elif LIBVLC_VERSION_MAJOR >= 3 && LIBVLC_VERSION_MINOR >= 0
# include <vlc_interface.h>
#else
 
#endif

#include <libvlc.h>
#include "defaults.h"
#include <vlc_fourcc.h>  // Definiciones de formatos de audio y video
#include <vlc_aout.h>    // Formatos de salida de audio
#include <vlc_vout_display.h>
#include <vlc_picture_pool.h>
#include <vlc_variables.h>
#include <vlc_input.h>
#include <vlc_playlist.h>
#include <new>
#include "interface_timer.h"



#define DEFAULT_THUMBNAIL_URI L"https://upload.wikimedia.org/wikipedia/commons/3/38/VLC_icon.png"

#define UNUSED(x) (void)(x)

static const char* const defaults_modelines_names[] = { N_("Manual"), N_("Automatic"), \
    N_("256x240  NTSC (60Hz)"), N_("320x240 NTSC (60Hz)"), \
    N_("320x480i NTSC (60Hz)"), N_("640x480i NTSC (60Hz)]"), N_("720x480i NTSC (60Hz)"), \
    N_("640x480p NTSC (60Hz)]"), N_("720x480p NTSC (60Hz)"), N_("256x240  PAL (50Hz)"), \
    N_("320x240  PAL (50Hz)"), N_("320x480i PAL (50Hz)"), N_("640x480i PAL (50Hz)"), \
    N_("720x576i PAL (50Hz)"), N_("640x480p PAL (50Hz)"), N_("720x576p PAL (50Hz)") };
static const int defaults_modelines_values_index[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 ,12, 13 ,14, 15 };


#define CFG_PREFIX "mister-groovy-"
#define MODELINE_DEFAULT_CFG CFG_PREFIX "modeline"
#define MODELINE_DEFAULT 1
#define BUFFER_SIZE 5
#define RUNTIME_CONFIG_POLL_CADENCE_DEFAULT 30

#define HOST_CFG        CFG_PREFIX "host"
#define COMPRESS_CFG    CFG_PREFIX "compress"

#define P_CLOCK_CFG     CFG_PREFIX "pClock"
#define H_ACTIVE_CFG    CFG_PREFIX "hActive"
#define H_BEGIN_CFG     CFG_PREFIX "hBegin" 
#define H_END_CFG       CFG_PREFIX "hEnd"  
#define H_TOTAL_CFG     CFG_PREFIX "hTotal" 
#define V_ACTIVE_CFG    CFG_PREFIX "vActive"
#define V_BEGIN_CFG     CFG_PREFIX "vBegin" 
#define V_END_CFG       CFG_PREFIX "vEnd"  
#define V_TOTAL_CFG     CFG_PREFIX "vTotal" 
#define INTERLACE_CFG   CFG_PREFIX "interlace"
#define ASPECT_RATIO_CFG CFG_PREFIX "aspectratio"
#define SMOOTH_CFG CFG_PREFIX "smoothvideo"
#define STREAM_LOG_CADENCE_CFG CFG_PREFIX "streamlogcadence"
#define RUNTIME_CONFIG_POLL_CADENCE_CFG CFG_PREFIX "runtimepollcadence"
#define ONLY15KHZ       CFG_PREFIX "only15khz"  
#define MIXAUDIO        CFG_PREFIX "mixaudio"  

static int OpenFilterAudio(vlc_object_t*);
static void CloseFilterAudio(vlc_object_t*);
static int OpenVoutDisplay(vlc_object_t*);
static void CloseVoutDisplay(vlc_object_t*);
static int OpenInterface(vlc_object_t*);
static void CloseInterface(vlc_object_t*);
static bool setConfig(vlc_object_t* p_obj, const video_stream::AutomaticModeInputs& automatic);
static video_stream::AutomaticModeInputs appliedAutomatic;

static modeline_struct* modeline = NULL;
static char*            Host = NULL;
static uint8_t          compress = 1;
static bool             aspectratio = false;
static bool             smoothvideo = true;
static int              videoWidth = 0;  // obtained from current video
static int              videoHeight = 0; // obtained from current video

static bool             timer_initialized = false;
static std::atomic<bool> audio_filter_active{false};
static std::atomic<bool> vout_display_active{false};
static unsigned long long vout_frame_seen_count = 0;
static unsigned long long stream_frame_count = 0;
static unsigned         stream_log_cadence = 300;
static unsigned         runtime_config_poll_cadence = RUNTIME_CONFIG_POLL_CADENCE_DEFAULT;
static int              current_modeline_cfg = MODELINE_DEFAULT;

struct vout_display_sys_t {
    picture_pool_t* pool;
};

static picture_pool_t* VoutPool(vout_display_t* vd, unsigned count)
{
    vout_display_sys_t* sys = vd->sys;
    if (sys == NULL) return NULL;
    if (sys->pool == NULL)
    {
        const unsigned poolCount = count > 0 ? count : 3;
        sys->pool = picture_pool_NewFromFormat(&vd->fmt, poolCount);
    }
    return sys->pool;
}

static int VoutControl(vout_display_t* vd, int query, va_list args)
{
    VLC_UNUSED(vd);
    VLC_UNUSED(args);

    switch (query)
    {
    case VOUT_DISPLAY_CHANGE_DISPLAY_SIZE:
    case VOUT_DISPLAY_CHANGE_DISPLAY_FILLED:
    case VOUT_DISPLAY_CHANGE_ZOOM:
    case VOUT_DISPLAY_CHANGE_SOURCE_ASPECT:
    case VOUT_DISPLAY_CHANGE_SOURCE_CROP:
    case VOUT_DISPLAY_CHANGE_VIEWPOINT:
    case VOUT_DISPLAY_RESET_PICTURES:
        return VLC_SUCCESS;
    default:
        return VLC_EGENERIC;
    }
}

static void VoutDisplay(vout_display_t* vd, picture_t* p_pic, subpicture_t*)
{
    vout_display_sys_t* sys = vd->sys;
    vout_frame_seen_count++;

    if (runtime_config_poll_cadence > 0 &&
        (vout_frame_seen_count % runtime_config_poll_cadence) == 0ULL)
    {
        const int requested_modeline = var_InheritInteger(vd, MODELINE_DEFAULT_CFG);
        const bool requested_aspect = var_InheritBool(vd, ASPECT_RATIO_CFG);
        auto automatic = appliedAutomatic;
        automatic.only15k = var_InheritBool(vd, ONLY15KHZ);
        if (vd->source.i_frame_rate && vd->source.i_frame_rate_base)
            automatic.updateRate(vd->source.i_frame_rate, vd->source.i_frame_rate_base);
        else if (p_pic)
            automatic.updateRate(p_pic->format.i_frame_rate, p_pic->format.i_frame_rate_base);

        if (requested_modeline != current_modeline_cfg || requested_aspect != aspectratio ||
            var_InheritBool(vd, SMOOTH_CFG) != smoothvideo ||
            automatic.requiresReselect(requested_modeline, appliedAutomatic))
        {
            const int previous_modeline = current_modeline_cfg;
            const bool previous_aspect = aspectratio;

            msg_Dbg(vd, "runtime request detected: modeline %d -> %d, aspect %d -> %d",
                previous_modeline,
                requested_modeline,
                (int)previous_aspect,
                (int)requested_aspect);

            msg_Dbg(vd, "runtime automatic inputs: only15khz=%d source_fps=%u/%u",
                (int)automatic.only15k, automatic.rate, automatic.base);
            if (setConfig((vlc_object_t*)vd, automatic))
            {
                // Publish mode + aspect together; capture stores its own copy.
                if (timer_initialized)
                    capture::changeModeline(modeline, aspectratio, smoothvideo);
                current_modeline_cfg = requested_modeline;
                msg_Dbg(vd, "runtime config updated: modeline=%d aspect=%d",
                    requested_modeline, (int)aspectratio);
            }
        }
    }

    if (sys != NULL)
    {
        if (!timer_initialized)
        {
            double fps = 25;
            if (vd->source.i_frame_rate_base != 0 && vd->source.i_frame_rate != 0)
                fps = double(vd->source.i_frame_rate) / vd->source.i_frame_rate_base;
            timer_initialized = capture::init_class(modeline, (vlc_object_t*)vd, aspectratio, smoothvideo, Host, compress, 0x00, 1500, fps);
        }

        if (capture::groovy_inicialiced())
        {
            // Picture buffers have no SAR in VLC; use the display source.
            const bool swapped = ORIENT_IS_SWAP(vd->source.orientation);
            const unsigned sarNum = swapped ? vd->source.i_sar_den : vd->source.i_sar_num;
            const unsigned sarDen = swapped ? vd->source.i_sar_num : vd->source.i_sar_den;
            if (capture::storeFrame(p_pic, sarNum, sarDen))
            {
                stream_frame_count++;
                if (stream_log_cadence > 0 && (stream_frame_count % stream_log_cadence) == 0ULL)
                {
                    msg_Dbg(vd, "vout stream stats: queued=%llu", stream_frame_count);
                }
            }
            else if (stream_log_cadence > 0 && (vout_frame_seen_count % stream_log_cadence) == 0ULL)
            {
                msg_Warn(vd, "frame rejected (chroma=%4.4s): invalid planes/size, mode change or unavailable buffer",
                    (const char*)&p_pic->format.i_chroma);
            }
        }
    }

    picture_Release(p_pic);
}


#if LIBVLC_VERSION_MAJOR >= 4
# define _add_bool(name, v, text, longtext, advc) \
    add_bool(name, v, text, longtext)
# define _add_integer(name, value, text, longtext, advc) \
    add_integer(name, value, text, longtext)
# define _add_integer_with_range(name, value, i_min, i_max, text, longtext, advc) \
    add_integer_with_range(name, value, i_min, i_max, text, longtext)
#else
# define _add_bool add_bool
# define _add_integer add_integer
# define _add_integer_with_range add_integer_with_range
#endif

#if LIBVLC_VERSION_MAJOR >= 4
# define _set_help(str) \
    set_help_html(str)
#else
# define _set_help(str) \
    set_help(str)
#endif

vlc_module_begin()
    set_shortname("Groovy Mister")
    set_description("Stream compositor output to Groovy Mister")
    set_capability("vout display", 0)
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VOUT)
    set_callbacks(OpenVoutDisplay, CloseVoutDisplay)
    _set_help(N_("<style>"
        "p { margin:0.5em 0 0.5em 0; }"
        "</style>"
        "<p>"
        "v" VERSION_STRING "<br>"
        "Copyright " VERSION_COPYRIGHT
        "</p>"
        "<p>"
        "Homepage: <a href=\"" VERSION_HOMEPAGE "\">" VERSION_HOMEPAGE "</a><br>"
        "</p>"));
    set_section(N_("General"), NULL)
    add_string(HOST_CFG, "",
        N_("Mister Host"),
        N_("Mister IP Address"), false);
    _add_bool(COMPRESS_CFG, 0,
        N_("Compress"),
        N_("Send Compress packets"), false);
    _add_integer_with_range(STREAM_LOG_CADENCE_CFG, 300, 0, 10000,
        N_("Stream log cadence"),
        N_("Write runtime stream stats every N frames (0 disables periodic logs)"), false);
    _add_integer_with_range(RUNTIME_CONFIG_POLL_CADENCE_CFG, RUNTIME_CONFIG_POLL_CADENCE_DEFAULT, 0, 10000,
        N_("Runtime config poll cadence"),
        N_("Read modeline, aspect, smoothing and Automatic 15 kHz changes every N frames (0 disables runtime polling)"), false);

    set_section(N_("Video & Audio"), NULL)
    _add_integer(MODELINE_DEFAULT_CFG, MODELINE_DEFAULT,
        N_("Video Mode"),
        N_("Select predifined modeline"), false)
    vlc_config_set(VLC_CONFIG_LIST, (size_t)(sizeof(defaults_modelines_values_index) / sizeof(int)),
        defaults_modelines_values_index, defaults_modelines_names);
    _add_bool(ASPECT_RATIO_CFG, 0,
        ("Aspect Ratio"),
        ("Preserves source display aspect ratio on a physical 4:3 screen"), false);
    _add_bool(SMOOTH_CFG, 1,
        ("Smooth video reduction"),
        ("Average source pixels when reducing video; disable for nearest-neighbor pixel art"), false);
    _add_bool(ONLY15KHZ, 1,
        ("15 kHz only (Automatic)"),
        ("Only affects Automatic video mode. Enabled: restrict to 15 kHz modelines. Disabled: allow all modelines, including 31 kHz; does not force a higher frequency. Saved changes apply during playback when runtime polling is enabled. Use only modes supported by your display."), false);
    add_integer(MIXAUDIO, 60, "Mix Audio front-rear",
        "Set a value from 0 to 100 for the adjustment", false)
    change_integer_range(0, 100) // Rango de 0 a 100

    set_section(N_("Modeline"), NULL)
    add_float(P_CLOCK_CFG, 0,
        N_("P_CLOCK"),
        N_("Pixel Clock"), false);
    _add_integer(H_ACTIVE_CFG, 0,
        N_("H_ACTIVE"),
        N_("Horizontal Active"), false);
    _add_integer(H_BEGIN_CFG, 0,
        N_("H_BEGIN"),
        N_("Horizontal Begin"), false);
    _add_integer(H_END_CFG, 0,
        N_("H_END"),
        N_("Horizontal End"), false);
    _add_integer(H_TOTAL_CFG, 0,
        N_("H_TOTAL"),
        N_("Horizontal Total"), false);
    _add_integer(V_ACTIVE_CFG, 0,
        N_("V_ACTIVE"),
        N_("Vertical Active"), false);
    _add_integer(V_BEGIN_CFG, 0,
        N_("V_BEGIN"),
        N_("Vertical Begin"), false);
    _add_integer(V_END_CFG, 0,
        N_("V_END"),
        N_("Vertical End"), false);
    _add_integer(V_TOTAL_CFG, 0,
        N_("V_TOTAL"),
        N_("Vertical Total"), false);
    _add_bool(INTERLACE_CFG, 0,
        N_("Interlace"),
        N_("Interlace"), false);
    add_submodule()
    set_capability("audio filter", 0)
    set_category(CAT_AUDIO)
    set_subcategory(SUBCAT_AUDIO_AFILTER)
    set_callbacks(OpenFilterAudio, CloseFilterAudio)
    add_submodule()
    set_capability("interface", 0)
#if LIBVLC_VERSION_MAJOR <= 3
    set_category(CAT_INTERFACE)
#endif
    set_subcategory(SUBCAT_INTERFACE_CONTROL)
    set_callbacks(OpenInterface, CloseInterface)
vlc_module_end()

    
// uh
using namespace std;


static bool setConfig(vlc_object_t* p_obj, const video_stream::AutomaticModeInputs& automatic)
{
    const int64_t requested = var_InheritInteger(p_obj, MODELINE_DEFAULT_CFG);
    const size_t count = sizeof(defaults_modelines) / sizeof(defaults_modelines[0]);
    if (!modeline || requested < 0 || uint64_t(requested) >= count) {
        msg_Err(p_obj, "Invalid modeline preset index");
        return false;
    }
    int selected = int(requested);
    bool nextAspect = var_InheritBool(p_obj, ASPECT_RATIO_CFG);
    if (selected == 1) {
        if (videoWidth <= 0 || videoHeight <= 0) return false;
        selected = video_stream::selectMode(unsigned(videoWidth), unsigned(videoHeight), automatic.rate, automatic.base,
            defaults_modelines, automatic.only15k);
        if (selected < 2) return false;
    }
    modeline_struct candidate{};
    if (selected > 1) candidate = defaults_modelines[selected];
    else {
        auto timing = [&](const char* key, uint16_t& value) {
            const int64_t raw = var_InheritInteger(p_obj, key);
            if (raw <= 0 || raw > UINT16_MAX) return false;
            value = uint16_t(raw);
            return true;
        };
        if (!timing(H_ACTIVE_CFG, candidate.hActive) || !timing(H_BEGIN_CFG, candidate.hBegin) ||
            !timing(H_END_CFG, candidate.hEnd) || !timing(H_TOTAL_CFG, candidate.hTotal) ||
            !timing(V_ACTIVE_CFG, candidate.vActive) || !timing(V_BEGIN_CFG, candidate.vBegin) ||
            !timing(V_END_CFG, candidate.vEnd) || !timing(V_TOTAL_CFG, candidate.vTotal)) {
            msg_Err(p_obj, "Manual timings must be in 1..65535");
            return false;
        }
        candidate.pClock = double(var_InheritFloat(p_obj, P_CLOCK_CFG));
        candidate.interlace = uint8_t(var_InheritBool(p_obj, INTERLACE_CFG));
    }
    if (!capture::validModeline(candidate)) {
        msg_Err(p_obj, "Invalid modeline: check timing order, frame capacity and even interlaced height");
        return false;
    }
    *modeline = candidate;
    appliedAutomatic = automatic;
    compress = uint8_t(var_InheritBool(p_obj, COMPRESS_CFG));
    aspectratio = nextAspect;
    smoothvideo = var_InheritBool(p_obj, SMOOTH_CFG);
    stream_log_cadence = unsigned((std::max)(int64_t(0), (std::min)(int64_t(10000),
        var_InheritInteger(p_obj, STREAM_LOG_CADENCE_CFG))));
    runtime_config_poll_cadence = unsigned((std::max)(int64_t(0), (std::min)(int64_t(10000),
        var_InheritInteger(p_obj, RUNTIME_CONFIG_POLL_CADENCE_CFG))));
    // Never save preferences from the presentation thread.
    msg_Dbg(p_obj, "Validated modeline %d: %ux%u interlace=%u pClock=%.6f",
        selected, candidate.hActive, candidate.vActive, candidate.interlace, candidate.pClock);
    if (requested == 1) {
        const double horizontal = candidate.pClock * 1000000.0 / candidate.hTotal;
        msg_Dbg(p_obj, "Automatic: source=%dx%d fps=%u/%u only15khz=%d -> preset=%d h=%.3f kHz refresh=%.3f Hz",
            videoWidth, videoHeight, automatic.rate, automatic.base, (int)automatic.only15k,
            selected, horizontal/1000.0, horizontal/candidate.vTotal*(candidate.interlace ? 2 : 1));
        if (!automatic.rate || !automatic.base)
            msg_Warn(p_obj, "Automatic source FPS unavailable; selecting by dimensions until FPS metadata arrives");
    }
    return true;
}

static void print_version(vlc_object_t* p_obj)
{
    msg_Dbg(p_obj, "v" VERSION_STRING ", " VERSION_COPYRIGHT ", " VERSION_LICENSE);
    msg_Dbg(p_obj, VERSION_HOMEPAGE);
}

static block_t* filterAudio(filter_t* p_filter, block_t* p_block)
{
    if (!p_block) return NULL;
    captureAudio::updateMixAudio(var_InheritInteger(p_filter, MIXAUDIO));
    captureAudio::StoreAudioBuffer(p_block);
    return p_block;
}

struct filter_sys_t { input_thread_t* input = nullptr; };

static int AudioInputEvent(vlc_object_t* input, const char*, vlc_value_t, vlc_value_t event, void*)
{
    if (event.i_int == INPUT_EVENT_STATE) {
        const int state = int(var_GetInteger(input, "state"));
        if (state == PLAYING_S || state == PAUSE_S)
            captureAudio::change_pause(state == PAUSE_S, mdate());
    }
    return VLC_SUCCESS;
}

static input_thread_t* HoldAudioInput(filter_t* filter)
{
    // Use the filter's owning playlist/input, not a process-wide object search.
    for (vlc_object_t* parent=filter->obj.parent; parent; parent=parent->obj.parent) {
        if (!strcmp(parent->obj.object_type, "input"))
            return static_cast<input_thread_t*>(vlc_object_hold(parent));
        if (!strcmp(parent->obj.object_type, "playlist"))
            return playlist_CurrentInput(reinterpret_cast<playlist_t*>(parent));
    }
    return nullptr;
}

static void FlushFilterAudio(filter_t*) { captureAudio::flush_audio(); }

static int OpenFilterAudio(vlc_object_t* p_this)
{
    filter_t* p_filter = (filter_t*)p_this;

    // The current converter reads native float32 only. Do not advertise FL64
    // or integer PCM, and do not send rates unrepresentable by CMD_INIT.
    const auto& input = p_filter->fmt_in.audio;
    if (input.i_format != VLC_CODEC_FL32 || !input.i_channels || input.i_channels > 32 ||
        !video_stream::AudioFormat{input.i_rate, 2}.valid())
    {
        msg_Err(p_filter, "Unsupported audio (%4.4s)",
            (char*)&input.i_format);
        return VLC_EGENERIC;
    }

#if DEBUG_GROOVY_VERBOSE
    msg_Dbg(p_filter, "format audio: %4.4s", (char*)&p_filter->fmt_in.audio.i_format);
#endif
    
    if (audio_filter_active.exchange(true)) {
        msg_Err(p_filter, "Another Groovy MiSTer audio filter is still active");
        return VLC_EGENERIC;
    }

    msg_Dbg(p_filter,
        "filter Audio sub-plugin opened: in_codec=%4.4s out_codec=%4.4s rate=%u channels=%u",
        (const char*)&p_filter->fmt_in.audio.i_format,
        (const char*)&p_filter->fmt_out.audio.i_format,
        input.i_rate,
        2u);
    p_filter->pf_audio_filter = filterAudio;
    p_filter->pf_flush = FlushFilterAudio;
    p_filter->p_sys = new (std::nothrow) filter_sys_t;
    if (!p_filter->p_sys) { audio_filter_active = false; return VLC_ENOMEM; }
    if (!captureAudio::init_audio(input.i_rate, 2, p_this)) {
        captureAudio::close_audio();
        delete p_filter->p_sys;
        p_filter->p_sys = nullptr;
        audio_filter_active = false;
        return VLC_ENOMEM;
    }
    p_filter->p_sys->input = HoldAudioInput(p_filter);
    if (p_filter->p_sys->input) {
        var_AddCallback(p_filter->p_sys->input, "intf-event", AudioInputEvent, nullptr);
        vlc_value_t event{}; event.i_int = INPUT_EVENT_STATE;
        AudioInputEvent(VLC_OBJECT(p_filter->p_sys->input), nullptr, {}, event, nullptr);
        msg_Dbg(p_filter, "Audio pause/resume observer attached to input");
    } else {
        msg_Warn(p_filter, "No owning input found: buffered audio cannot follow pause/resume clock shifts");
    }

    return VLC_SUCCESS;
}

static void CloseFilterAudio(vlc_object_t* p_this)
{
    filter_t* filter = (filter_t*)p_this;
    if (filter->p_sys) {
        if (filter->p_sys->input) {
            // Remove callbacks before taking the audio mutex or releasing state.
            var_DelCallback(filter->p_sys->input, "intf-event", AudioInputEvent, nullptr);
            vlc_object_release(filter->p_sys->input);
        }
        delete filter->p_sys;
        filter->p_sys = nullptr;
    }
    captureAudio::close_audio();
    audio_filter_active = false;
    msg_Dbg(p_this, "filter Audio sub-plugin closed; video remains independent");
}


// --- Config helper interface ---------------------------------------------
// Persistent submodule that fills the modeline H/V fields from the selected
// Video Mode preset even when no video is playing. It polls the Video Mode
// option on a timer and, when it changes to a predefined preset, writes the
// derived fields and saves the configuration so they show up in preferences.
struct intf_sys_t {
    vlc_object_t* object;
    int last_synced_modeline = -999;
    InterfaceTimer timer;
    explicit intf_sys_t(vlc_object_t* owner) : object(owner) {}
};

static void syncModelinePreset(void* data)
{
    auto* sys = static_cast<intf_sys_t*>(data);
    vlc_object_t* p_obj = sys->object;

    const int mode = (int)config_GetInt(p_obj, MODELINE_DEFAULT_CFG);
    if (mode == sys->last_synced_modeline)
        return;
    sys->last_synced_modeline = mode;

    const int count = (int)(sizeof(defaults_modelines_values_index) / sizeof(defaults_modelines_values_index[0]));
    if (mode > 1 && mode < count)
    {
        const modeline_struct* m = &defaults_modelines[mode];
        config_PutInt(p_obj, H_ACTIVE_CFG, m->hActive);
        config_PutInt(p_obj, H_BEGIN_CFG,  m->hBegin);
        config_PutInt(p_obj, H_END_CFG,    m->hEnd);
        config_PutInt(p_obj, H_TOTAL_CFG,  m->hTotal);
        config_PutInt(p_obj, V_ACTIVE_CFG, m->vActive);
        config_PutInt(p_obj, V_BEGIN_CFG,  m->vBegin);
        config_PutInt(p_obj, V_END_CFG,    m->vEnd);
        config_PutInt(p_obj, V_TOTAL_CFG,  m->vTotal);
        config_PutInt(p_obj, INTERLACE_CFG, m->interlace);
        config_PutFloat(p_obj, P_CLOCK_CFG, (float)m->pClock);
        config_SaveConfigFile(p_obj);
        msg_Dbg(p_obj, "modeline preset %d synced to fields", mode);
    }
}

static int OpenInterface(vlc_object_t* p_this)
{
    auto* intf = reinterpret_cast<intf_thread_t*>(p_this);
    intf->p_sys = new (std::nothrow) intf_sys_t(p_this);
    if (!intf->p_sys) return VLC_ENOMEM;
    if (!intf->p_sys->timer.start(syncModelinePreset, intf->p_sys, CLOCK_FREQ)) {
        delete intf->p_sys;
        intf->p_sys = nullptr;
        return VLC_EGENERIC;
    }
    msg_Dbg(p_this, "groovy mister config helper interface opened");
    return VLC_SUCCESS;
}

static void CloseInterface(vlc_object_t* p_this)
{
    auto* intf = reinterpret_cast<intf_thread_t*>(p_this);
    if (intf->p_sys) {
        intf->p_sys->timer.stop(); // Join callback while sys/owner are still alive.
        delete intf->p_sys;
        intf->p_sys = nullptr;
    }
    msg_Dbg(p_this, "groovy mister config helper interface closed");
}


static bool isOpaqueHwChroma(vlc_fourcc_t chroma)
{
    switch (chroma)
    {
    case VLC_CODEC_D3D9_OPAQUE:
    case VLC_CODEC_D3D9_OPAQUE_10B:
    case VLC_CODEC_D3D11_OPAQUE:
    case VLC_CODEC_D3D11_OPAQUE_10B:
    case VLC_CODEC_MMAL_OPAQUE:
    case VLC_CODEC_ANDROID_OPAQUE:
    case VLC_CODEC_VAAPI_420:
    case VLC_CODEC_VAAPI_420_10BPP:
        return true;
    default:
        return false;
    }
}

static int OpenVoutDisplay(vlc_object_t* p_this)
{
    vout_display_t* vd = (vout_display_t*)p_this;

    // Reject GPU-opaque surfaces so VLC falls back to software decoding/conversion
    // instead of silently dropping every frame (this plugin needs readable pixels).
    if (isOpaqueHwChroma(vd->fmt.i_chroma))
    {
        msg_Err(p_this, "unsupported hardware-accelerated chroma %4.4s; disable hardware decoding",
            (const char*)&vd->fmt.i_chroma);
        return VLC_EGENERIC;
    }

    // Capture/API storage is process-global: an overlapping vout must not reset
    // another instance's buffers or close its worker.
    if (vout_display_active.exchange(true)) {
        msg_Err(p_this, "Another Groovy MiSTer video output is still active");
        return VLC_EGENERIC;
    }

    // Ask VLC to convert packed RGB/YUV and rotate before handing us CPU planes.
    // Preserve full-range input through J420; only these two layouts are read below.
    video_format_t normalized;
    video_format_ApplyRotation(&normalized, &vd->fmt);
    normalized.i_chroma = (vd->fmt.b_color_range_full || vd->fmt.i_chroma == VLC_CODEC_J420)
        ? VLC_CODEC_J420 : VLC_CODEC_I420;
    normalized.b_color_range_full = normalized.i_chroma == VLC_CODEC_J420;
    vd->fmt = normalized;

    vout_display_sys_t* sys = (vout_display_sys_t*)calloc(1, sizeof(*sys));
    if (sys == NULL) {
        vout_display_active = false;
        return VLC_ENOMEM;
    }

    sys->pool = NULL;
    vd->sys = sys;
    timer_initialized = false;
    stream_frame_count = 0;
    vout_frame_seen_count = 0;

    videoWidth = normalized.i_visible_width;
    videoHeight = normalized.i_visible_height;
    video_stream::AutomaticModeInputs automatic;
    automatic.only15k = var_InheritBool(p_this, ONLY15KHZ);
    automatic.updateRate(normalized.i_frame_rate, normalized.i_frame_rate_base);
    automatic.updateRate(vd->source.i_frame_rate, vd->source.i_frame_rate_base);

    if (modeline == NULL)
        modeline = (struct modeline_struct*)calloc(1, sizeof(modeline_struct));

    if (!setConfig(p_this, automatic))
    {
        free(sys);
        vd->sys = NULL;
        free(modeline);
        modeline = NULL;
        vout_display_active = false;
        return VLC_EGENERIC;
    }
    current_modeline_cfg = int(var_InheritInteger(p_this, MODELINE_DEFAULT_CFG));

    // Read the host once. var_InheritString has an inline free() for empty
    // strings; use VLC's allocator counterpart across the MSVC/MinGW boundary.
    vlc_value_t hostValue{};
    if (var_Inherit(p_this, HOST_CFG, VLC_VAR_STRING, &hostValue) != VLC_SUCCESS ||
        !hostValue.psz_string || !*hostValue.psz_string) {
        libvlc_free(hostValue.psz_string);
        free(sys);
        vd->sys = NULL;
        free(modeline);
        modeline = NULL;
        vout_display_active = false;
        msg_Err(p_this, "A Groovy MiSTer host address is required");
        return VLC_EGENERIC;
    }
    Host = hostValue.psz_string;

    vd->pool = VoutPool;
    vd->prepare = NULL;
    vd->display = VoutDisplay;
    vd->control = VoutControl;

    msg_Dbg(p_this, "vout display sub-plugin opened (log_cadence=%u, runtime_poll=%u)",
        stream_log_cadence,
        runtime_config_poll_cadence);
    return VLC_SUCCESS;
}

static void CloseVoutDisplay(vlc_object_t* p_this)
{
    vout_display_t* vd = (vout_display_t*)p_this;
    vout_display_sys_t* sys = vd->sys;

    if (sys != NULL)
    {
        if (sys->pool != NULL)
            picture_pool_Release(sys->pool);
        free(sys);
        vd->sys = NULL;
    }

    msg_Dbg(p_this, "vout stream summary: queued=%llu", stream_frame_count);

    capture::close();
    libvlc_free(Host);
    Host = NULL;
    if (modeline != NULL)
    {
        free(modeline);
        modeline = NULL;
    }
    timer_initialized = false;
    vout_display_active = false;
    vout_frame_seen_count = 0;

    msg_Dbg(p_this, "vout display sub-plugin closed");
}


