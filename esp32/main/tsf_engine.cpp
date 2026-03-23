/*
 * TSF (TinySoundFont) Drum Engine
 *
 * Wraps the TinySoundFont library as an independent synth engine
 * for drum/sample playback alongside the Dexed FM engine.
 *
 * Memory layout:
 *   - SFO file loaded via tsf_load_memory() during init
 *   - Decoded float samples stored in PSRAM (~6 MB for full RX5)
 *   - MIDI queue and render buffers in internal SRAM
 *
 * Threading:
 *   - tsf_engine_handle_midi() called from any core (sequencer, MIDI tasks)
 *   - tsf_engine_render_stereo_i16() called only from audio_render_task (Core 1)
 */

#include "tsf_engine.h"
#include "esp32_config.h"

/* TSF header — declarations only; implementation is compiled in tsf_impl.cpp */
#include "tsf.h"

/* Reclaims the 2 MB stb_vorbis decode workspace allocated in tsf_impl.cpp. */
extern "C" void tsf_free_vorbis_buf(void);

/* Diagnostic getters from tsf_impl.cpp */
extern "C" int  tsf_impl_get_failure_reason(void);
extern "C" int  tsf_impl_get_last_vorbis_error(void);
extern "C" int  tsf_impl_get_alloc_fail_count(void);
extern "C" void tsf_impl_dump_stats(void);
extern "C" void tsf_impl_reset_counters(void);

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "tsf_engine";

/* =====================================================================
 * Configuration
 * ===================================================================== */

#define TSF_SFO_PATH        TSF_DEFAULT_SFO_PATH
#define TSF_MAX_VOICES      TSF_DEFAULT_MAX_VOICES
#define TSF_GAIN_DB         TSF_DEFAULT_GAIN_DB

/* Convenience macro: log PSRAM + internal free heap on one line */
#define LOG_HEAP(label) \
    ESP_LOGI(TAG, "[heap] %s: PSRAM_free=%lu  internal_free=%lu  total_free=%lu", \
             (label), \
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), \
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), \
             (unsigned long)esp_get_free_heap_size())

/* =====================================================================
 * State
 * ===================================================================== */

static tsf *s_tsf = nullptr;
static volatile bool s_loaded = false;     /* checked from audio thread */
static volatile bool s_reloading = false;  /* pause rendering during reload */
static volatile int8_t s_load_progress = 0; /* 0=idle, 1-7=loading, 8=done, -1..=-5=failed */
static volatile uint8_t s_error_detail = 0; /* TSF_ERR_* bitmask */
static int s_sample_rate = 48000;

/* =====================================================================
 * MIDI queue — same pattern as dexed_raw.cpp
 *
 * Lock-free ring buffer protected by a portMUX spinlock.
 * Push from protocol/sequencer tasks (Core 0), pop from render (Core 1).
 * IRAM_ATTR to avoid I-cache eviction from USB/Flash on Core 0.
 * ===================================================================== */

struct TsfMidiMsg {
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
};

static constexpr uint16_t kQueueSize = 128; /* power of 2 */
static TsfMidiMsg s_midi_q[kQueueSize];
static volatile uint16_t s_midi_q_head = 0;
static volatile uint16_t s_midi_q_tail = 0;
static portMUX_TYPE s_midi_q_mux = portMUX_INITIALIZER_UNLOCKED;

static IRAM_ATTR void tsf_midi_q_push(uint8_t status, uint8_t data1, uint8_t data2)
{
    portENTER_CRITICAL(&s_midi_q_mux);
    const uint16_t head = s_midi_q_head;
    const uint16_t next = (uint16_t)((head + 1) & (kQueueSize - 1));
    if (next != s_midi_q_tail) {
        s_midi_q[head] = TsfMidiMsg{status, data1, data2};
        s_midi_q_head = next;
    }
    portEXIT_CRITICAL(&s_midi_q_mux);
}

static IRAM_ATTR bool tsf_midi_q_pop(TsfMidiMsg &out)
{
    bool ok = false;
    portENTER_CRITICAL(&s_midi_q_mux);
    const uint16_t tail = s_midi_q_tail;
    if (tail != s_midi_q_head) {
        out = s_midi_q[tail];
        s_midi_q_tail = (uint16_t)((tail + 1) & (kQueueSize - 1));
        ok = true;
    }
    portEXIT_CRITICAL(&s_midi_q_mux);
    return ok;
}

/* =====================================================================
 * SFO loading
 * ===================================================================== */

static int load_sfo(const char *path)
{
    s_load_progress = 0;
    s_error_detail  = 0;
    tsf_impl_reset_counters();

    ESP_LOGI(TAG, "=== TSF SFO load starting: %s ===", path);
    LOG_HEAP("load_start");

    /* ----------------------------------------------------------------
     * Step 1: Open file and validate it exists
     * ---------------------------------------------------------------- */
    ESP_LOGI(TAG, "[tsf 1/5] Opening SFO file: %s", path);
    FILE *f = fopen(path, "rb");
    if (!f) {
        s_error_detail |= TSF_ERR_FILE_NOT_FOUND;
        ESP_LOGE(TAG, "[tsf 1/5] FAILED — file not found: %s", path);
        ESP_LOGE(TAG, "[tsf]   Is SPIFFS mounted? Is drums.sfo flashed?");
        s_load_progress = -1;
        return -1;
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    ESP_LOGI(TAG, "[tsf 1/5] File opened: %ld bytes (%.1f KB)", fsize,
             (double)fsize / 1024.0);

    if (fsize <= 0) {
        s_error_detail |= TSF_ERR_FILE_SIZE;
        ESP_LOGE(TAG, "[tsf 1/5] FAILED — file empty or ftell error (%ld)", fsize);
        fclose(f);
        s_load_progress = -2;
        return -1;
    }
    if (fsize > 4 * 1024 * 1024) {
        s_error_detail |= TSF_ERR_FILE_SIZE;
        ESP_LOGE(TAG, "[tsf 1/5] FAILED — file too large: %ld bytes (max 4 MB)", fsize);
        fclose(f);
        s_load_progress = -2;
        return -1;
    }

    /* Read and log first 12 bytes (RIFF header) for diagnostics */
    uint8_t hdr[12] = {};
    (void)fread(hdr, 1, sizeof(hdr), f);
    ESP_LOGI(TAG, "[tsf 1/5] Header: %02X%02X%02X%02X size=0x%02X%02X%02X%02X type=%02X%02X%02X%02X"
             "  ('%c%c%c%c')",
             hdr[0], hdr[1], hdr[2], hdr[3],
             hdr[7], hdr[6], hdr[5], hdr[4],
             hdr[8], hdr[9], hdr[10], hdr[11],
             (char)hdr[0], (char)hdr[1], (char)hdr[2], (char)hdr[3]);
    if (hdr[0] != 'R' || hdr[1] != 'I' || hdr[2] != 'F' || hdr[3] != 'F') {
        ESP_LOGW(TAG, "[tsf 1/5] WARNING: does not start with 'RIFF' — may not be valid SF2/SF3");
    }

    /* Close header-check handle; tsf_load_filename() opens its own. */
    fclose(f);
    f = nullptr;
    s_load_progress = 1;
    ESP_LOGI(TAG, "[tsf 1/5] File validated OK");

    /* ----------------------------------------------------------------
     * Step 2: Decode SFO via tsf_load_filename() — streams directly from
     * SPIFFS using fread/fseek(SEEK_CUR) without a full-file PSRAM buffer.
     * tsf_load_filename() uses the same tsf_stream stdio callbacks as the
     * manual tsf_stream approach, and is simpler to call.
     *
     * Peak PSRAM usage here:
     *   TSF smpo copy:      ~401 KB   (TSF_MALLOC inside tsf_load)
     *   Vorbis workspace:   ~384 KB   (kVorbisAllocSize in tsf_impl.cpp)
     *   Float PCM peak:     ~6.5 MB   (capped by 128K-step realloc in tsf.h)
     *   ─────────────────────────────
     *   Peak total:         ~7.3 MB   (comfortably fits in 8 MB PSRAM)
     * ---------------------------------------------------------------- */
    ESP_LOGI(TAG, "[tsf 2/5] Calling tsf_load_filename() — OGG decode starts...");
    ESP_LOGI(TAG, "[tsf 2/5] This may take several seconds (decoding all drum samples).");
    LOG_HEAP("before_tsf_load");
    s_load_progress = 2;

    const uint64_t t0 = esp_timer_get_time();
    tsf *t = tsf_load_filename(path);

    /* Free the stb_vorbis decode workspace; raw OGG copy freed by TSF itself. */
    tsf_free_vorbis_buf();

    /* Dump tsf_impl diagnostics regardless of outcome */
    tsf_impl_dump_stats();
    LOG_HEAP("after_tsf_load");

    if (!t) {
        int reason    = tsf_impl_get_failure_reason();
        int verr      = tsf_impl_get_last_vorbis_error();
        int afails    = tsf_impl_get_alloc_fail_count();

        ESP_LOGE(TAG, "[tsf 2/5] FAILED — tsf_load_filename() returned NULL");
        ESP_LOGE(TAG, "[tsf]   failure_reason=%d  vorbis_error=%d  alloc_fails=%d",
                 reason, verr, afails);

        switch (reason) {
            case 0:
                ESP_LOGE(TAG, "[tsf]   Reason=0 (unknown): possible SF2/SF3 structure error");
                ESP_LOGE(TAG, "[tsf]   Try re-flashing drums.sfo");
                s_error_detail |= TSF_ERR_VORBIS_DECODE;
                break;
            case 1:
                ESP_LOGE(TAG, "[tsf]   Reason=1: PSRAM exhausted (TSF_MALLOC failure)");
                ESP_LOGE(TAG, "[tsf]   Peak = TSF smpo copy (~401 KB) + vorbis ws (384 KB)"
                         " + float buf (~6.5 MB) = ~7.3 MB");
                ESP_LOGE(TAG, "[tsf]   Check heap log above for free PSRAM");
                s_error_detail |= TSF_ERR_TSF_MALLOC;
                break;
            case 2:
                ESP_LOGE(TAG, "[tsf]   Reason=2: stb_vorbis decode error (error=%d)", verr);
                ESP_LOGE(TAG, "[tsf]   Corrupt or unsupported OGG stream in drums.sfo");
                s_error_detail |= TSF_ERR_VORBIS_DECODE;
                break;
            case 3:
                ESP_LOGE(TAG, "[tsf]   Reason=3: vorbis bump-allocator OOM");
                ESP_LOGE(TAG, "[tsf]   Increase kVorbisAllocSize (currently 384 KB)");
                s_error_detail |= TSF_ERR_VORBIS_OOM;
                break;
        }
        if (afails > 0) {
            s_error_detail |= TSF_ERR_TSF_MALLOC;
        }

        ESP_LOGE(TAG, "[tsf]   error_detail=0x%02X", (unsigned)s_error_detail);
        s_load_progress = -2;
        return -1;
    }

    const uint64_t load_us = esp_timer_get_time() - t0;
    ESP_LOGI(TAG, "[tsf 2/5] tsf_load() SUCCEEDED in %llu ms (%.1f s)",
             (unsigned long long)(load_us / 1000),
             (double)(load_us / 1000) / 1000.0);
    LOG_HEAP("after_decode_success");
    s_load_progress = 3;

    /* ----------------------------------------------------------------
     * Step 3: Configure output and channel
     * ---------------------------------------------------------------- */
    ESP_LOGI(TAG, "[tsf 3/5] Configuring output: rate=%d Hz, gain=%.1f dB, max_voices=%d",
             s_sample_rate, (double)TSF_GAIN_DB, TSF_MAX_VOICES);
    tsf_set_output(t, TSF_STEREO_INTERLEAVED, s_sample_rate, TSF_GAIN_DB);
    tsf_set_max_voices(t, TSF_MAX_VOICES);

    /* Find the drum preset: try bank 128 (GM percussion), then bank 0. */
    int pi = tsf_get_presetindex(t, 128, 0);
    ESP_LOGI(TAG, "[tsf 3/5] tsf_get_presetindex(bank=128, prog=0) = %d", pi);
    if (pi < 0) {
        pi = tsf_get_presetindex(t, 0, 0);
        ESP_LOGI(TAG, "[tsf 3/5] tsf_get_presetindex(bank=0, prog=0)  = %d", pi);
    }
    if (pi < 0) {
        /* Try every preset and pick the first one that looks percussive */
        int n = tsf_get_presetcount(t);
        for (int i = 0; i < n && pi < 0; i++) {
            const char *name = tsf_get_presetname(t, i);
            ESP_LOGI(TAG, "[tsf 3/5]   scanning preset %d: '%s'", i, name);
            pi = i;  /* take the first one as fallback */
        }
        ESP_LOGW(TAG, "[tsf 3/5] Using first available preset as fallback: %d", pi);
    }

    if (pi >= 0) {
        tsf_channel_set_presetindex(t, 9, pi);
        ESP_LOGI(TAG, "[tsf 3/5] Ch9 preset set: index=%d bank=%d prog=%d name='%s'",
                 pi,
                 tsf_channel_get_preset_bank(t, 9),
                 tsf_channel_get_preset_number(t, 9),
                 tsf_get_presetname(t, pi));
    } else {
        ESP_LOGW(TAG, "[tsf 3/5] No preset found — drums will be silent");
    }

    /* Log all presets for diagnostics */
    const int npresets = tsf_get_presetcount(t);
    ESP_LOGI(TAG, "[tsf 3/5] SFO contains %d preset(s):", npresets);
    for (int i = 0; i < npresets; i++) {
        ESP_LOGI(TAG, "[tsf]   [%2d] '%s'  bank=%d prog=%d",
                 i, tsf_get_presetname(t, i),
                 tsf_channel_get_preset_bank(t, i),
                 tsf_channel_get_preset_number(t, i));
    }
    s_load_progress = 4;

    /* ----------------------------------------------------------------
     * Step 4: Swap in new TSF instance
     * ---------------------------------------------------------------- */
    ESP_LOGI(TAG, "[tsf 4/5] Swapping in new TSF instance (presets=%d, voices_max=%d)",
             npresets, TSF_MAX_VOICES);
    tsf *old = s_tsf;
    s_tsf = t;
    s_loaded = true;
    s_load_progress = 5;

    if (old) {
        ESP_LOGI(TAG, "[tsf 4/5] Closing old TSF instance");
        tsf_close(old);
    }

    LOG_HEAP("engine_ready");
    ESP_LOGI(TAG, "=== TSF engine READY: %ld bytes file, %d presets, %llu ms ===",
             fsize, npresets, (unsigned long long)(load_us / 1000));

    /* Remap progress=5 to the public API value of 8 for "fully loaded" */
    s_load_progress = 8;
    return 0;
}

/* =====================================================================
 * Public API
 * ===================================================================== */

int tsf_engine_init(int sample_rate)
{
    s_sample_rate = sample_rate;
    s_loaded = false;
    s_reloading = false;
    s_midi_q_head = 0;
    s_midi_q_tail = 0;
    s_error_detail = 0;

    ESP_LOGI(TAG, "=== TSF drum engine init: sample_rate=%d Hz, path='%s' ===",
             sample_rate, TSF_SFO_PATH);
    LOG_HEAP("tsf_engine_init_entry");

    int ret = load_sfo(TSF_SFO_PATH);
    if (ret != 0) {
        ESP_LOGE(TAG, "TSF init FAILED (error_detail=0x%02X) — drums will use Dexed fallback",
                 (unsigned)s_error_detail);
    }
    return ret;
}

void tsf_engine_deinit(void)
{
    s_loaded = false;
    s_reloading = false;
    if (s_tsf) {
        tsf_close(s_tsf);
        s_tsf = nullptr;
    }
    ESP_LOGI(TAG, "TSF engine deinitialized");
    LOG_HEAP("tsf_engine_deinit");
}

bool tsf_engine_is_loaded(void)
{
    return s_loaded && !s_reloading;
}

void IRAM_ATTR tsf_engine_handle_midi(uint8_t status, uint8_t data1, uint8_t data2)
{
    tsf_midi_q_push(status, data1, data2);
}

void tsf_engine_render_stereo_i16(int16_t *out, int num_samples)
{
    /* Clear output buffer (TSF_STEREO_INTERLEAVED expects pre-zeroed for mixing) */
    memset(out, 0, (size_t)(num_samples * 2) * sizeof(int16_t));

    if (!s_loaded || s_reloading || !s_tsf) {
        return;
    }

    /* Drain MIDI queue — process up to 64 messages per block.
     * TSF uses the channel API: channel 9 = GM drums. */
    TsfMidiMsg msg;
    int processed = 0;
    while (processed < 64 && tsf_midi_q_pop(msg)) {
        uint8_t type = msg.status & 0xF0;
        /* Use channel 9 (drums) regardless of the incoming MIDI channel */
        switch (type) {
            case 0x90: /* Note On */
                if (msg.data2 > 0) {
                    tsf_channel_note_on(s_tsf, 9, msg.data1,
                                        (float)msg.data2 / 127.0f);
                } else {
                    tsf_channel_note_off(s_tsf, 9, msg.data1);
                }
                break;
            case 0x80: /* Note Off */
                tsf_channel_note_off(s_tsf, 9, msg.data1);
                break;
            case 0xB0: /* Control Change */
                tsf_channel_midi_control(s_tsf, 9, msg.data1, msg.data2);
                break;
            default:
                break;
        }
        processed++;
    }

    /* Render audio */
    tsf_render_short(s_tsf, out, num_samples, 0);
}

int tsf_engine_reload(void)
{
    ESP_LOGI(TAG, "=== TSF reload requested ===");
    s_reloading = true;

    /* Brief delay to ensure the render task sees s_reloading */
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Close old instance */
    s_loaded = false;
    if (s_tsf) {
        tsf_close(s_tsf);
        s_tsf = nullptr;
    }

    int ret = load_sfo(TSF_SFO_PATH);
    s_reloading = false;
    return ret;
}

int tsf_engine_active_voices(void)
{
    if (!s_loaded || !s_tsf) return 0;
    return tsf_active_voice_count(s_tsf);
}

int tsf_engine_get_load_progress(void)
{
    return (int)s_load_progress;
}

uint8_t tsf_engine_get_error_detail(void)
{
    return s_error_detail;
}
