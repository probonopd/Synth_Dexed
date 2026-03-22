/*
 * ESP32-S3 Port - Raw Dexed Engine Wrapper
 */

#include "dexed_raw.h"
#include "esp32_config.h"

#include "dexed.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/portmacro.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "dexed_raw";

static Dexed *s_dexed = nullptr;
static void *s_dexed_mem = nullptr;
static bool s_dexed_mem_psram = false;
static SemaphoreHandle_t s_mutex = NULL;

static bool s_bank_loaded = false;
static uint8_t s_bank_voices[32][156];

// -----------------------------------------------------------------------------
// MIDI queue: push from protocol tasks, consume on audio task.
// This prevents priority inversion / stalls that can cause crackles.
// -----------------------------------------------------------------------------

struct MidiMsg {
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
};

static constexpr uint16_t kMidiQueueSize = 256; // power of 2
static MidiMsg s_midi_q[kMidiQueueSize];
static volatile uint16_t s_midi_q_head = 0;
static volatile uint16_t s_midi_q_tail = 0;
static portMUX_TYPE s_midi_q_mux = portMUX_INITIALIZER_UNLOCKED;

// SysEx queue: copy SysEx data from protocol tasks, consume on audio task.
// This avoids taking the engine mutex from non-audio tasks (which can block
// the audio thread and cause audible glitches).
static constexpr int kSysexMaxLen = 4200;
static uint8_t s_sysex_msg[2][kSysexMaxLen];
static uint16_t s_sysex_len[2] = {0, 0};
static volatile uint8_t s_sysex_wr = 0;
static volatile uint8_t s_sysex_rd = 0;
static portMUX_TYPE s_sysex_mux = portMUX_INITIALIZER_UNLOCKED;

/* IRAM_ATTR: these spinlock-protected queue functions are called from both
 * cores in the hot path.  Placing them in IRAM prevents I-cache eviction
 * by USB DMA activity on Core 0, which would widen the cross-core spinlock
 * contention window and inject variable latency into the audio thread. */
static IRAM_ATTR bool sysex_q_push(const uint8_t *data, int len)
{
    if (!data || len <= 0 || len > kSysexMaxLen) return false;
    bool ok = false;
    portENTER_CRITICAL(&s_sysex_mux);
    const uint8_t wr = s_sysex_wr;
    const uint8_t next = (uint8_t)((wr + 1) & 1);
    if (next != s_sysex_rd) {
        memcpy(s_sysex_msg[wr], data, (size_t)len);
        s_sysex_len[wr] = (uint16_t)len;
        s_sysex_wr = next;
        ok = true;
    }
    portEXIT_CRITICAL(&s_sysex_mux);
    return ok;
}

static IRAM_ATTR bool sysex_q_pop(const uint8_t **data, int *len)
{
    if (!data || !len) return false;
    bool ok = false;
    portENTER_CRITICAL(&s_sysex_mux);
    const uint8_t rd = s_sysex_rd;
    if (rd != s_sysex_wr) {
        *data = s_sysex_msg[rd];
        *len = (int)s_sysex_len[rd];
        s_sysex_rd = (uint8_t)((rd + 1) & 1);
        ok = true;
    }
    portEXIT_CRITICAL(&s_sysex_mux);
    return ok;
}

static volatile int s_last_active_voices = 0;

static IRAM_ATTR void midi_q_push(uint8_t status, uint8_t data1, uint8_t data2)
{
    portENTER_CRITICAL(&s_midi_q_mux);
    const uint16_t head = s_midi_q_head;
    const uint16_t next = (uint16_t)((head + 1) & (kMidiQueueSize - 1));
    if (next != s_midi_q_tail) {
        s_midi_q[head] = MidiMsg{status, data1, data2};
        s_midi_q_head = next;
    }
    portEXIT_CRITICAL(&s_midi_q_mux);
}

static IRAM_ATTR bool midi_q_pop(MidiMsg &out)
{
    bool ok = false;
    portENTER_CRITICAL(&s_midi_q_mux);
    const uint16_t tail = s_midi_q_tail;
    if (tail != s_midi_q_head) {
        out = s_midi_q[tail];
        s_midi_q_tail = (uint16_t)((tail + 1) & (kMidiQueueSize - 1));
        ok = true;
    }
    portEXIT_CRITICAL(&s_midi_q_mux);
    return ok;
}

static bool lock_engine(TickType_t ticks)
{
    if (!s_mutex) return false;
    return xSemaphoreTake(s_mutex, ticks) == pdTRUE;
}

static void unlock_engine(void)
{
    if (s_mutex) xSemaphoreGive(s_mutex);
}

int dexed_raw_init(int sample_rate)
{
    if (s_dexed) return 0;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create engine mutex");
        return -1;
    }

    // Force Dexed engine object into internal SRAM.
    //
    // sizeof(Dexed) >= 16 KB because the class embeds a 4096-element int32
    // render scratch buffer (q32_buffer_).  With CONFIG_SPIRAM_USE_MALLOC=y and
    // ALWAYSINTERNAL=4096, plain malloc() routes allocations > 4 KB to PSRAM.
    // PSRAM and Flash share the SPI0/SPI1 bus on ESP32-S3.  When the USB host
    // library executes code from Flash (non-IRAM), those SPI0 reads stall any
    // concurrent PSRAM access from the audio render task on Core 1 → render
    // overruns → I2S underruns → audible crackle every time a USB device is
    // attached.  Forcing Dexed into internal SRAM removes this bus conflict
    // entirely.
    ESP_LOGI(TAG, "Dexed object size: %u bytes", (unsigned)sizeof(Dexed));
    void *mem = heap_caps_malloc(sizeof(Dexed), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_dexed_mem_psram = false;
    if (!mem) {
        // Internal SRAM exhausted; fall back to PSRAM.  Crackles may persist
        // when USB is active — but at least the engine starts.  Reduce
        // FMRACK_POLYPHONY or FMRACK_NUM_MODULES to fit in internal SRAM.
        ESP_LOGW(TAG, "Not enough internal SRAM for Dexed (%u bytes), using PSRAM",
                 (unsigned)sizeof(Dexed));
        mem = heap_caps_malloc(sizeof(Dexed), MALLOC_CAP_SPIRAM);
        s_dexed_mem_psram = (mem != NULL);
    }
    if (!mem) {
        ESP_LOGE(TAG, "Failed to allocate Dexed");
        return -1;
    }
    ESP_LOGI(TAG, "Dexed allocated in %s",
             s_dexed_mem_psram ? "PSRAM (fallback — USB crackle workaround may be needed)"
                               : "internal SRAM (optimal)");

    s_dexed_mem = mem;

    // One raw Dexed instance, polyphony controlled by config macro.
    const uint8_t poly = (uint8_t)FMRACK_POLYPHONY;
    s_dexed = new (mem) Dexed(poly, static_cast<uint16_t>(sample_rate));
    s_dexed->loadInitVoice();
    s_dexed->setMonoMode(false);
    s_dexed->setNoteRefreshMode(false);
    s_dexed->setMaxNotes(poly);

    // choose engine type (MSFA/MKI/OPL) according to compile‑time setting
    s_dexed->setEngineType((uint8_t)FMRACK_ENGINE);
    s_dexed->activate();

    // ---- Controller defaults ----
    // Pitch bend: ±2 semitones (standard DX7 default).
    s_dexed->setPitchbendRange(2);
    s_dexed->setPitchbendStep(0);   // smooth (not quantised)
    // Mod wheel: route to pitch (vibrato) with moderate depth.
    // Without these calls the wheel range/target defaults to 0 and the
    // modwheel has no audible effect.
    s_dexed->setModWheelRange(50);  // 0-99; 50 gives a useful vibrato sweep
    s_dexed->setModWheelTarget(1);  // bit0=pitch, bit1=amp, bit2=EG

    static const char *engine_names[] = {"MSFA","MKI","OPL"};
    const char *ename = "?";
    if (FMRACK_ENGINE < (int)(sizeof(engine_names)/sizeof(engine_names[0])))
        ename = engine_names[FMRACK_ENGINE];

    ESP_LOGI(TAG, "Dexed raw engine initialized (sample_rate=%d, engine=%s, poly=%u)",
             sample_rate, ename, poly);
    return 0;
}

void dexed_raw_deinit(void)
{
    if (s_dexed) {
        s_dexed->deactivate();
        s_dexed->~Dexed();
        if (s_dexed_mem_psram) {
            heap_caps_free(s_dexed_mem);
        } else {
            free(s_dexed_mem);
        }
        s_dexed = nullptr;
        s_dexed_mem = nullptr;
        s_dexed_mem_psram = false;
    }
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
}

bool dexed_raw_is_initialized(void)
{
    return s_dexed != nullptr;
}

int dexed_raw_get_active_voices(void)
{
    return s_dexed ? s_last_active_voices : 0;
}

void dexed_raw_process_audio(float *left_out, float *right_out, int num_samples)
{
    if (!s_dexed || !left_out || !right_out || num_samples <= 0) {
        if (left_out) memset(left_out, 0, num_samples * sizeof(float));
        if (right_out) memset(right_out, 0, num_samples * sizeof(float));
        return;
    }

    static int16_t *s_tmp = nullptr;
    static int s_tmp_cap = 0;
    if (s_tmp_cap < num_samples) {
        if (s_tmp) free(s_tmp);
        s_tmp = (int16_t *)malloc(num_samples * sizeof(int16_t));
        s_tmp_cap = s_tmp ? num_samples : 0;
    }
    if (!s_tmp) {
        memset(left_out, 0, num_samples * sizeof(float));
        memset(right_out, 0, num_samples * sizeof(float));
        return;
    }

    if (!lock_engine(portMAX_DELAY)) {
        memset(left_out, 0, num_samples * sizeof(float));
        memset(right_out, 0, num_samples * sizeof(float));
        return;
    }

    // Consume a bounded number of queued MIDI messages each audio block.
    // Program changes are applied by swapping the currently loaded voice.
    MidiMsg msg;
    int processed = 0;
    while (processed < 64 && midi_q_pop(msg)) {
        const uint8_t st = (uint8_t)(msg.status & 0xF0);
        if (st == 0xC0 && s_bank_loaded) {
            const int program0 = (int)(msg.data1 & 0x7F);
            if (program0 >= 0 && program0 < 32) {
                s_dexed->loadVoiceParameters(s_bank_voices[program0]);
            }
        } else {
            uint8_t midiData[3] = { msg.status, msg.data1, msg.data2 };
            const uint8_t channel = (uint8_t)((msg.status & 0x0F) + 1);
            s_dexed->midiDataHandler(channel, midiData, 3);
        }
        processed++;
    }

    // Consume at most one queued SysEx message per audio block.
    // (SysEx is rare; this keeps CPU bounded.)
    const uint8_t *sx = nullptr;
    int sx_len = 0;
    if (sysex_q_pop(&sx, &sx_len)) {
        const uint8_t ch = (sx_len > 2 && sx[1] == 0x43) ? (uint8_t)((sx[2] & 0x0F) + 1) : 1;
        s_dexed->midiDataHandler(ch, const_cast<uint8_t *>(sx), (int16_t)sx_len);
    }

    s_dexed->getSamples(s_tmp, static_cast<uint16_t>(num_samples));

    s_last_active_voices = (int)s_dexed->getNumNotesPlaying();
    unlock_engine();

    // Convert mono int16 to stereo float (center pan).
    // Matches existing FMRack Module scaling (rough Dexed output normalization).
    const float scale = 1.0f / 9000.0f;
    for (int i = 0; i < num_samples; ++i) {
        const float s = static_cast<float>(s_tmp[i]) * scale;
        left_out[i] = s;
        right_out[i] = s;
    }
}

// ---------------------------------------------------------------------------
// Direct int16 mono output – skips float conversion entirely.
// The raw Dexed output is attenuated by ~1/2.5 to match the headroom of the
// previous float path (scale 1/9000 * 32767 ≈ 3.64 → we keep it at ~1:1
// for the I2S DAC and let the user set gain via the Dexed gain parameter).
// ---------------------------------------------------------------------------
/* IRAM_ATTR: the audio-task entry into the render pipeline must not
 * suffer a cache miss at the start of each 5.8 ms block. */
IRAM_ATTR void dexed_raw_process_audio_i16(int16_t *out, int num_samples)
{
    if (!s_dexed || !out || num_samples <= 0) {
        if (out) memset(out, 0, num_samples * sizeof(int16_t));
        return;
    }

    if (!lock_engine(portMAX_DELAY)) {
        memset(out, 0, num_samples * sizeof(int16_t));
        return;
    }

    // Consume queued MIDI messages on the audio thread.
    MidiMsg msg;
    int processed = 0;
    while (processed < 64 && midi_q_pop(msg)) {
        const uint8_t st = (uint8_t)(msg.status & 0xF0);
        if (st == 0xC0 && s_bank_loaded) {
            const int program0 = (int)(msg.data1 & 0x7F);
            if (program0 >= 0 && program0 < 32) {
                s_dexed->loadVoiceParameters(s_bank_voices[program0]);
            }
        } else {
            uint8_t midiData[3] = { msg.status, msg.data1, msg.data2 };
            const uint8_t channel = (uint8_t)((msg.status & 0x0F) + 1);
            s_dexed->midiDataHandler(channel, midiData, 3);
        }
        processed++;
    }

    // Consume at most one queued SysEx message per audio block.
    const uint8_t *sx = nullptr;
    int sx_len = 0;
    if (sysex_q_pop(&sx, &sx_len)) {
        const uint8_t ch = (sx_len > 2 && sx[1] == 0x43) ? (uint8_t)((sx[2] & 0x0F) + 1) : 1;
        s_dexed->midiDataHandler(ch, const_cast<uint8_t *>(sx), (int16_t)sx_len);
    }

    // getSamples writes 16-bit PCM directly into `out`.
    s_dexed->getSamples(out, static_cast<uint16_t>(num_samples));

    s_last_active_voices = (int)s_dexed->getNumNotesPlaying();
    unlock_engine();
}

void dexed_raw_handle_midi(uint8_t status, uint8_t data1, uint8_t data2)
{
    if (!s_dexed) return;

    // Push to queue; handled on audio thread.
    midi_q_push(status, data1, data2);
}

void dexed_raw_handle_sysex(const uint8_t *data, int len, uint8_t channel)
{
    (void)channel;
    if (!s_dexed || !data || len <= 0) return;
    // Push to SysEx queue; handled on audio thread.
    (void)sysex_q_push(data, len);
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

static int parse_voice_data_line(const char *value, uint8_t out[156])
{
    int count = 0;
    const char *p = value;
    while (*p && count < 156) {
        while (*p && isspace((unsigned char)*p)) ++p;
        if (!*p) break;

        int hi = hex_nibble(*p++);
        if (hi < 0) return -1;
        int lo = hex_nibble(*p++);
        if (lo < 0) return -1;
        out[count++] = static_cast<uint8_t>((hi << 4) | lo);

        while (*p && !isspace((unsigned char)*p)) ++p;
    }
    return (count == 156) ? 0 : -1;
}

int dexed_raw_load_voice_from_performance_ini(const char *path)
{
    if (!s_dexed || !path) return -1;

    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGW(TAG, "Could not open %s", path);
        return -1;
    }

    char line[4096];
    int rc = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VoiceData1=", 10) != 0) continue;
        uint8_t voice[156] = {0};
        if (parse_voice_data_line(line + 10, voice) == 0) {
            if (lock_engine(pdMS_TO_TICKS(100))) {
                s_dexed->loadVoiceParameters(voice);
                unlock_engine();
                ESP_LOGI(TAG, "Loaded VoiceData1 from %s", path);
                rc = 0;
            }
        }
        break;
    }

    fclose(f);
    return rc;
}

int dexed_raw_send_sysex_file(const char *path, uint8_t channel)
{
    if (!s_dexed || !path) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Could not open SysEx file: %s", path);
        return -1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long sz = ftell(f);
    if (sz <= 0 || sz > 8192) {
        fclose(f);
        ESP_LOGW(TAG, "SysEx file size unsupported: %ld", sz);
        return -1;
    }
    rewind(f);

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return -1;
    }

    const size_t nread = fread(buf, 1, (size_t)sz, f);
    fclose(f);

    if (nread != (size_t)sz) {
        free(buf);
        return -1;
    }

    dexed_raw_handle_sysex(buf, (int)nread, channel);
    free(buf);
    return 0;
}

static bool decode_dx7_packed_128_to_dexed_156(uint8_t out156[156], const uint8_t in128[128])
{
    if (!out156 || !in128) return false;

    // This mirrors Dexed::decodeVoice(), but writes into a standalone buffer.
    memset(out156, 0, 156);

    for (uint8_t op = 0; op < 6; ++op) {
        // Copy 11 bytes of EG + scaling breakpoint/depths
        memcpy(&out156[op * 21], &in128[op * 17], 11);

        uint8_t tmp = in128[(op * 17) + 11];
        out156[DEXED_OP_SCL_LEFT_CURVE + (op * 21)]  = (tmp & 0x03);
        out156[DEXED_OP_SCL_RGHT_CURVE + (op * 21)]  = (tmp & 0x0c) >> 2;

        tmp = in128[(op * 17) + 12];
        out156[DEXED_OP_OSC_DETUNE + (op * 21)]      = (tmp & 0x78) >> 3;
        out156[DEXED_OP_OSC_RATE_SCALE + (op * 21)]  = (tmp & 0x07);

        tmp = in128[(op * 17) + 13];
        out156[DEXED_OP_KEY_VEL_SENS + (op * 21)]    = (tmp & 0x1c) >> 2;
        out156[DEXED_OP_AMP_MOD_SENS + (op * 21)]    = (tmp & 0x03);

        out156[DEXED_OP_OUTPUT_LEV + (op * 21)]      = in128[(op * 17) + 14];

        tmp = in128[(op * 17) + 15];
        out156[DEXED_OP_FREQ_COARSE + (op * 21)]     = (tmp & 0x3e) >> 1;
        out156[DEXED_OP_OSC_MODE + (op * 21)]        = (tmp & 0x01);

        out156[DEXED_OP_FREQ_FINE + (op * 21)]       = in128[(op * 17) + 16];
    }

    // Pitch EG (8 bytes)
    memcpy(&out156[DEXED_VOICE_OFFSET], &in128[102], 8);

    uint8_t tmp = in128[110];
    out156[DEXED_VOICE_OFFSET + DEXED_ALGORITHM] = (tmp & 0x1f);

    tmp = in128[111];
    out156[DEXED_VOICE_OFFSET + DEXED_OSC_KEY_SYNC] = (tmp & 0x08) >> 3;
    out156[DEXED_VOICE_OFFSET + DEXED_FEEDBACK]     = (tmp & 0x07);

    // LFO speed/delay/PMD/AMD (4 bytes)
    memcpy(&out156[DEXED_VOICE_OFFSET + DEXED_LFO_SPEED], &in128[112], 4);

    tmp = in128[116];
    out156[DEXED_VOICE_OFFSET + DEXED_LFO_PITCH_MOD_SENS] = (tmp & 0x30) >> 4;
    out156[DEXED_VOICE_OFFSET + DEXED_LFO_WAVE]           = (tmp & 0x0e) >> 1;
    out156[DEXED_VOICE_OFFSET + DEXED_LFO_SYNC]           = (tmp & 0x01);

    out156[DEXED_VOICE_OFFSET + DEXED_TRANSPOSE] = in128[117];
    memcpy(&out156[DEXED_VOICE_OFFSET + DEXED_NAME], &in128[118], 10);

    // Operator enable bitmask (all ops on)
    out156[155] = 0x3F;
    return true;
}

int dexed_raw_load_voice_from_bank_syx(const char *path, int voice_index0)
{
    if (!s_dexed || !path) return -1;
    if (voice_index0 < 0 || voice_index0 > 31) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Could not open bank SysEx: %s", path);
        return -1;
    }

    uint8_t syx[4104];
    const size_t n = fread(syx, 1, sizeof(syx), f);
    fclose(f);
    if (n != sizeof(syx)) {
        ESP_LOGW(TAG, "Bank SysEx wrong size (%u), expected 4104", (unsigned)n);
        return -1;
    }

    // Bulk bank: voice data starts at offset 6, 32 voices * 128 bytes
    if (syx[0] != 0xF0 || syx[1] != 0x43 || syx[4103] != 0xF7) {
        ESP_LOGW(TAG, "Bank SysEx header/footer mismatch");
        return -1;
    }

    const size_t base = 6 + (size_t)voice_index0 * 128;
    uint8_t voice156[156];
    if (!decode_dx7_packed_128_to_dexed_156(voice156, &syx[base])) {
        return -1;
    }

    if (!lock_engine(pdMS_TO_TICKS(200))) return -1;
    s_dexed->loadVoiceParameters(voice156);
    unlock_engine();

    char name[11] = {0};
    memcpy(name, &voice156[145], 10);
    ESP_LOGI(TAG, "Loaded bank voice %d: '%s'", voice_index0 + 1, name);
    return 0;
}

int dexed_raw_load_bank_syx(const char *path)
{
    if (!s_dexed || !path) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Could not open bank SysEx: %s", path);
        return -1;
    }

    uint8_t syx[4104];
    const size_t n = fread(syx, 1, sizeof(syx), f);
    fclose(f);
    if (n != sizeof(syx)) {
        ESP_LOGW(TAG, "Bank SysEx wrong size (%u), expected 4104", (unsigned)n);
        return -1;
    }

    if (syx[0] != 0xF0 || syx[1] != 0x43 || syx[4103] != 0xF7) {
        ESP_LOGW(TAG, "Bank SysEx header/footer mismatch");
        return -1;
    }

    for (int i = 0; i < 32; ++i) {
        const size_t base = 6 + (size_t)i * 128;
        if (!decode_dx7_packed_128_to_dexed_156(s_bank_voices[i], &syx[base])) {
            s_bank_loaded = false;
            return -1;
        }
    }

    s_bank_loaded = true;
    ESP_LOGI(TAG, "Cached 32 voices from bank: %s", path);
    return 0;
}

int dexed_raw_select_bank_program(int voice_index0)
{
    if (!s_dexed || !s_bank_loaded) return -1;
    if (voice_index0 < 0 || voice_index0 > 31) return -1;

    if (!lock_engine(pdMS_TO_TICKS(200))) return -1;
    s_dexed->loadVoiceParameters(s_bank_voices[voice_index0]);
    s_last_active_voices = (int)s_dexed->getNumNotesPlaying();
    unlock_engine();
    return 0;
}
