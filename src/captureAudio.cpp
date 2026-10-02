#include "captureAudio.h"
#include <mutex>
#include "audio_buffer_limits.h"
#include "audio_timeline.h"
#include "audio_ring_submit.h"

#define MAX_AUDIO_SECONDS 3      // Máximo tiempo de almacenamiento en segundos


static vlc_object_t* t_object;
static uint32_t soundRate;
static uint8_t soundChan = 2;
static bool m_initialized_audio = false;


static int16_t* audioBuffer = NULL; // Buffer circular dinámico
static size_t writeIndex = 0;  // Índice de escritura en el buffer
static size_t readIndex = 0;  // Índice de lectura en el buffer
static audio_stream::Timeline audioTimeline;
static int64_t& audioBasePTS = audioTimeline.base;
static std::recursive_mutex audioMutex; // Lives across filter open/close cycles.
static int buffer_samples = 0;
static int64_t& lastVideoPTS = audioTimeline.last_video;
static int64_t& lastAudioPTS = audioTimeline.end; // End of the final buffered block.
static int max_audio_samples = 0;
static uint64_t audioPauseRebases = 0, audioFlushes = 0, pausedBlocks = 0;
static int mix = 60;


int64_t samples2time(int samples)
{
    return ((uint64_t)samples * 1000000) / (soundRate * soundChan);
}




int16_t* convertAudio32to16stereo(block_t* p_block, int& num_samples_out) {
    if (!p_block) return nullptr;
    filter_t* p_filter = (filter_t*)t_object;
    audio_format_t fmt = p_filter->fmt_in.audio;

    int num_channels_in = fmt.i_channels;  // Canales de entrada
    int num_channels_out = 2; // Siempre convertimos a estéreo
    int num_samples = p_block->i_buffer / (sizeof(float) * num_channels_in); // Muestras por canal

    // Número total de muestras en la salida (estéreo)
    num_samples_out = num_samples * num_channels_out;

    // Crear buffer dinámico para almacenar los datos convertidos a 16 bits estéreo
    int16_t* audioBuffer32 = (int16_t*)malloc(num_samples_out * sizeof(int16_t));

    if (audioBuffer32 == nullptr) {
        msg_Err(p_filter, "Error on Malloc convertAudio32to16");
        return nullptr;
    }

    // Puntero a los datos de audio en flotante
    float* pDataFloat = (float*)p_block->p_buffer;

    float boostFactor = (num_channels_in > 2) ? 2.0f : 1.0f;

    // Convertir de float 32-bit a uint16_t con mezcla de canales
    for (int i = 0; i < num_samples; i++) {
        float sampleL = 0.0f, sampleR = 0.0f;

        if (num_channels_in == 1) {
            // Mono --> Copiar el mismo valor en ambos canales
            sampleL = sampleR = pDataFloat[i];
        }
        else if (num_channels_in == 2) {
            // Estéreo --> Mantener los valores originales
            sampleL = pDataFloat[i * 2];
            sampleR = pDataFloat[i * 2 + 1];
        }
        else {
            // Más de 2 canales --> Mezcla estándar (3F2R: 5.0 Surround)
            float frontL = pDataFloat[i * num_channels_in];
            float frontC = (num_channels_in >= 3) ? pDataFloat[i * num_channels_in + 1] : 0.0f;
            float frontR = (num_channels_in >= 3) ? pDataFloat[i * num_channels_in + 2] : 0.0f;
            float rearL = (num_channels_in >= 4) ? pDataFloat[i * num_channels_in + 3] : 0.0f;
            float rearR = (num_channels_in >= 5) ? pDataFloat[i * num_channels_in + 4] : 0.0f;

            // Mezcla: L = (FrontL + Center + RearL) / 3, R = (FrontR + Center + RearR) / 3
            //sampleL = (frontL + frontC + rearL) / 3.0f;
            //sampleR = (frontR + frontC + rearR) / 3.0f;

            float mixfront = (float)mix / 100.0f;
            float mixrear = 1.0f - mixfront;

            // Mezcla con ponderación: 60% frontales, 40% traseros
            sampleL = (mixfront * (frontL + frontC) + mixrear * rearL) / 2.0f;
            sampleR = (mixfront * (frontR + frontC) + mixrear * rearR) / 2.0f;
        }

        sampleL *= boostFactor;
        sampleR *= boostFactor;

        // Clamping: Asegurar que los valores están en [-1.0, 1.0]
        sampleL = fmaxf(fminf(sampleL, 1.0f), -1.0f);
        sampleR = fmaxf(fminf(sampleR, 1.0f), -1.0f);

        // Convertir a uint16_t en rango [32767, -32767]
        audioBuffer32[i * 2] = (int16_t)(sampleL * 32767.0f);
        audioBuffer32[i * 2 + 1] = (int16_t)(sampleR * 32767.0f);
    }

    return audioBuffer32; // Devuelve el buffer convertido
}

int16_t* convertAudio32to16(block_t* p_block, int& num_samples_out) {
    if (!p_block) return nullptr;
    filter_t* p_filter = (filter_t*)t_object;
    audio_format_t fmt = p_filter->fmt_in.audio;

    int num_channels_in = fmt.i_channels;  // Canales de entrada
    int num_channels_out = (fmt.i_channels <= 4) ? fmt.i_channels : 4;
    int num_samples = p_block->i_buffer / (sizeof(float) * num_channels_in); // Muestras por canal

    // Número total de muestras en la salida
    num_samples_out = num_samples * num_channels_out;

    // Crear buffer dinámico para almacenar los datos convertidos a 16 bits estéreo
    int16_t* audioBuffer32 = (int16_t*)malloc(num_samples_out * sizeof(int16_t));

    if (audioBuffer32 == nullptr) {
        msg_Err(p_filter, "Error on Malloc convertAudio32to16");
        return nullptr;
    }

    // Puntero a los datos de audio en flotante
    float* pDataFloat = (float*)p_block->p_buffer;

    //float boostFactor = (num_channels_in > 2) ? 2.0f : 1.0f;

    // Convertir de float 32-bit a uint16_t con mezcla de canales
    for (int i = 0; i < num_samples; i++) {

        if (num_channels_in == 1) {
            // Mono --> Copiar el mismo valor en ambos canales
            audioBuffer32[i * 2] = (int16_t)(pDataFloat[i] * 32767.0f);
        }
        else if (num_channels_in == 2) {
            // Estéreo --> Mantener los valores originales
            float sampleL = pDataFloat[i * 2];
            float sampleR = pDataFloat[i * 2 + 1];
            // Clamping: Asegurar que los valores están en [-1.0, 1.0]
            sampleL = fmaxf(fminf(sampleL, 1.0f), -1.0f);
            sampleR = fmaxf(fminf(sampleR, 1.0f), -1.0f);

            // Convertir a uint16_t en rango [32767, -32767]
            audioBuffer32[i * 2] = (int16_t)(sampleL * 32767.0f);
            audioBuffer32[i * 2 + 1] = (int16_t)(sampleR * 32767.0f);
        }
        else if (num_channels_in == 4) {
            // Estéreo --> Mantener los valores originales
            float sampleL = pDataFloat[i * 4];
            float sampleR = pDataFloat[i * 4 + 1];
            float rearL = pDataFloat[i * 4 + 2];
            float rearR = pDataFloat[i * 4 + 3];

            // Clamping: Asegurar que los valores están en [-1.0, 1.0]
            sampleL = fmaxf(fminf(sampleL, 1.0f), -1.0f);
            sampleR = fmaxf(fminf(sampleR, 1.0f), -1.0f);
            rearL = fmaxf(fminf(rearL, 1.0f), -1.0f);
            rearR = fmaxf(fminf(rearR, 1.0f), -1.0f);

            // Convertir a uint16_t en rango [32767, -32767]
            audioBuffer32[i * 4] = (int16_t)(sampleL * 32767.0f);
            audioBuffer32[i * 4 + 1] = (int16_t)(sampleR * 32767.0f);
            audioBuffer32[i * 4 + 2] = (int16_t)(rearL * 32767.0f);
            audioBuffer32[i * 4 + 3] = (int16_t)(rearR * 32767.0f);
        }
        else {
            // Más de 2 canales --> Mezcla estándar (3F2R: 5.0 Surround)
            float frontL = pDataFloat[i * num_channels_in];
            float frontC = (num_channels_in >= 3) ? pDataFloat[i * num_channels_in + 1] : 0.0f;
            float frontR = (num_channels_in >= 3) ? pDataFloat[i * num_channels_in + 2] : 0.0f;
            float rearL = (num_channels_in >= 4) ? pDataFloat[i * num_channels_in + 3] : 0.0f;
            float rearR = (num_channels_in >= 5) ? pDataFloat[i * num_channels_in + 4] : 0.0f;

            float mixfront = (float)mix / 100.0f;
            float mixrear = 1.0f - mixfront;

            // Mezcla con ponderación: mixfront% frontal, mixrear% frontales
            float sampleL = (mixfront * frontC + mixrear * frontL) / 2.0f;
            float sampleR = (mixfront * frontC + mixrear * frontR) / 2.0f;

            // Clamping: Asegurar que los valores están en [-1.0, 1.0]
            sampleL = fmaxf(fminf(sampleL, 1.0f), -1.0f);
            sampleR = fmaxf(fminf(sampleR, 1.0f), -1.0f);
            rearL = fmaxf(fminf(rearL, 1.0f), -1.0f);
            rearR = fmaxf(fminf(rearR, 1.0f), -1.0f);

            // Convertir a uint16_t en rango [32767, -32767]
            audioBuffer32[i * 4] = (int16_t)(sampleL * 32767.0f);
            audioBuffer32[i * 4 + 1] = (int16_t)(sampleR * 32767.0f);
            audioBuffer32[i * 4 + 2] = (int16_t)(rearL * 32767.0f);
            audioBuffer32[i * 4 + 3] = (int16_t)(rearR * 32767.0f);
        }
    }

    return audioBuffer32; // Devuelve el buffer convertido
}



bool captureAudio::init_audio(uint32_t sndRate, uint8_t snddChan, vlc_object_t* object)
{
    std::lock_guard<std::recursive_mutex> lifecycleLock(audioMutex);
    if (!video_stream::AudioFormat{sndRate, snddChan}.valid()) return false;
    if (!m_initialized_audio)
    {
        soundRate = sndRate;
        soundChan = snddChan;
        max_audio_samples = (soundRate * MAX_AUDIO_SECONDS * soundChan);
        audioPauseRebases = audioFlushes = pausedBlocks = 0;
        m_initialized_audio = false;
        buffer_samples = 0;
        audioBasePTS = 0;
        writeIndex = 0;
        readIndex = 0;
        lastVideoPTS = 0;
        lastAudioPTS = 0;

        t_object = object;
        audioBuffer = (int16_t*)malloc(max_audio_samples * sizeof(int16_t));
        if (audioBuffer) {
            m_initialized_audio = true;
            memset(audioBuffer, 0, max_audio_samples * sizeof(int16_t)); // Inicializar en 0
        }
        msg_Dbg(t_object, "Capture AudioInit.");
    }
    return m_initialized_audio;
}

video_stream::AudioFormat captureAudio::format()
{
    std::lock_guard<std::recursive_mutex> lock(audioMutex);
    return m_initialized_audio ? video_stream::AudioFormat{soundRate, soundChan}
        : video_stream::AudioFormat{};
}

static void flushAudioLocked()
{
    buffer_samples = 0;
    writeIndex = readIndex = 0;
    audioTimeline.flush();
}

void captureAudio::change_pause(bool paused, int64_t date)
{
    std::lock_guard<std::recursive_mutex> lock(audioMutex);
    const int64_t shifted = audioTimeline.changePause(paused, date);
    if (shifted < 0) flushAudioLocked();
    else if (shifted > 0) {
        ++audioPauseRebases;
        if (t_object) msg_Dbg(t_object, "Audio resumed: shifted buffered timestamps by %lld us, samples=%d",
            shifted, buffer_samples);
    }
}

void captureAudio::flush_audio()
{
    std::lock_guard<std::recursive_mutex> lock(audioMutex);
    flushAudioLocked();
    ++audioFlushes;
    if (t_object) msg_Dbg(t_object, "Audio queue flushed by VLC");
}

void captureAudio::StoreAudioBuffer(block_t* p_block)
{
    std::lock_guard<std::recursive_mutex> lock(audioMutex);
#if !DEBUG_GROOVY
    if (!capture::groovy_inicialiced()) return;
#endif
    if (!m_initialized_audio || !audioBuffer || !t_object || !p_block ||
        p_block->i_pts <= VLC_TS_INVALID) return;
    if (audioTimeline.paused) { ++pausedBlocks; return; }
    int num_samples = 0;
    int16_t* samples = convertAudio32to16stereo(p_block, num_samples);
    if (!samples || num_samples <= 0) { free(samples); return; }
    int64_t pts = p_block->i_pts;
    if ((p_block->i_flags & BLOCK_FLAG_DISCONTINUITY) || audioTimeline.discontinuity(pts)) {
        flushAudioLocked();
        ++audioFlushes;
        msg_Dbg(t_object, "Audio queue reset at discontinuity, new PTS=%lld", pts);
    }
    const int skip = (std::max)(0, num_samples-max_audio_samples);
    pts += samples2time(skip);
    num_samples -= skip;
    if (!buffer_samples) audioBasePTS = pts;
    const int overwritten = (std::max)(0, buffer_samples+num_samples-max_audio_samples);
    if (overwritten) {
        readIndex = (readIndex+overwritten)%max_audio_samples;
        audioBasePTS += samples2time(overwritten);
        buffer_samples -= overwritten;
    }
    for (int i=0; i<num_samples; ++i) {
        audioBuffer[writeIndex] = samples[skip+i];
        writeIndex = (writeIndex+1)%max_audio_samples;
    }
    buffer_samples += num_samples;
    lastAudioPTS = pts + samples2time(num_samples);
    free(samples);
}

void captureAudio::add_audio_to_recording(int64_t videoPTS, int m_field, double videofr,
    video_stream::AudioFormat negotiated)
{
    (void)videofr; // Keep the existing timestamp-based selection, not guessed FPS.
    std::lock_guard<std::recursive_mutex> lifecycleLock(audioMutex);
    // Recheck under the buffer lock: the filter may have changed since the
    // worker inspected its format. Never consume/send samples in the old mode.
    if (!video_stream::canSendAudio(format(), negotiated)) return;
    if (!m_initialized_audio || m_field != 0 || audioTimeline.paused) return;
#if !DEBUG_GROOVY
    if (!capture::groovy_inicialiced()) return;
    gmw_fpgaStatus status{};
    gmw_getStatus(&status);
    if (!status.audio) return;
    int16_t* destination = reinterpret_cast<int16_t*>(gmw_get_pBufferAudio());
#else
    static int16_t scratch[audio_stream::max_audio_chunk_samples];
    int16_t* destination = scratch;
#endif
    if (!destination) return;
    const size_t requested = audioTimeline.readable(videoPTS, buffer_samples,
        max_audio_samples, soundRate, soundChan);
    const size_t submitted = audio_stream::submitRing(audioBuffer, size_t(max_audio_samples),
        readIndex, size_t(buffer_samples), requested, destination, audio_stream::max_audio_chunk_samples,
        [](size_t samples) {
#if !DEBUG_GROOVY
            gmw_audio(uint16_t(samples * sizeof(int16_t)));
#else
            (void)samples;
#endif
        });
    if (!submitted) return; // Do not consume a PTS if audio/buffer is unavailable.
    buffer_samples -= int(submitted);
    readIndex = (readIndex + submitted) % size_t(max_audio_samples);
    // Round once for the whole read, preserving the old timeline exactly.
    audioBasePTS += samples2time(int(submitted));
    lastVideoPTS = videoPTS;
}

void captureAudio::updateMixAudio(int mixaudio)
{
    std::lock_guard<std::recursive_mutex> lifecycleLock(audioMutex);
    mix = mixaudio;
}

bool captureAudio::close_audio()
{
    std::lock_guard<std::recursive_mutex> lifecycleLock(audioMutex);
    if (m_initialized_audio)
    {
        msg_Dbg(t_object, "Close Audio GMW: pause_rebases=%llu flushes=%llu paused_blocks=%llu",
            audioPauseRebases, audioFlushes, pausedBlocks);
        m_initialized_audio = false;
        soundRate = 0;
        soundChan = 0;
        buffer_samples = 0;
        audioBasePTS = 0;
        writeIndex = 0;
        readIndex = 0;
        lastVideoPTS = 0;
        lastAudioPTS = 0;
        max_audio_samples = 0;
        t_object = nullptr;
        if (audioBuffer) {
            free(audioBuffer);
            audioBuffer = nullptr;
        }
    }
    audioTimeline = {};
    t_object = nullptr; // Also clear a failed allocation's borrowed filter.
    return true;
}
