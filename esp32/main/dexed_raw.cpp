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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "dexed_raw";

static Dexed *s_dexed = nullptr;
static void *s_dexed_mem = nullptr;
static bool s_dexed_mem_psram = false;
static SemaphoreHandle_t s_mutex = NULL;

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

    void *mem = heap_caps_malloc(sizeof(Dexed), MALLOC_CAP_SPIRAM);
    if (!mem) {
        ESP_LOGW(TAG, "PSRAM allocation failed for Dexed, using internal memory");
        mem = malloc(sizeof(Dexed));
        if (!mem) {
            ESP_LOGE(TAG, "Failed to allocate Dexed");
            return -1;
        }
        s_dexed_mem_psram = false;
    } else {
        s_dexed_mem_psram = true;
    }

    s_dexed_mem = mem;

    // One raw Dexed instance, polyphony 16 notes.
    s_dexed = new (mem) Dexed(static_cast<uint8_t>(16), static_cast<uint16_t>(sample_rate));
    s_dexed->loadInitVoice();
    s_dexed->setMonoMode(false);
    s_dexed->setNoteRefreshMode(false);
    s_dexed->setMaxNotes(16);
    s_dexed->setEngineType(0);
    s_dexed->activate();

    ESP_LOGI(TAG, "Dexed raw engine initialized (sample_rate=%d)", sample_rate);
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
    if (!s_dexed) return 0;
    if (!lock_engine(pdMS_TO_TICKS(2))) return 0;
    const int n = static_cast<int>(s_dexed->getNumNotesPlaying());
    unlock_engine();
    return n;
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

    s_dexed->getSamples(s_tmp, static_cast<uint16_t>(num_samples));
    unlock_engine();

    // Convert mono int16 to stereo float (center pan).
    // Matches existing FMRack Module scaling (rough Dexed output normalization).
    const float scale = 1.0f / 3000.0f;
    for (int i = 0; i < num_samples; ++i) {
        const float s = static_cast<float>(s_tmp[i]) * scale;
        left_out[i] = s;
        right_out[i] = s;
    }
}

void dexed_raw_handle_midi(uint8_t status, uint8_t data1, uint8_t data2)
{
    if (!s_dexed) return;

    uint8_t midiData[3] = { status, data1, data2 };
    const uint8_t msgChannel = static_cast<uint8_t>((status & 0x0F) + 1);

    if (!lock_engine(pdMS_TO_TICKS(10))) return;
    s_dexed->midiDataHandler(msgChannel, midiData, 3);
    unlock_engine();
}

void dexed_raw_handle_sysex(const uint8_t *data, int len, uint8_t channel)
{
    if (!s_dexed || !data || len <= 0) return;
    if (!lock_engine(pdMS_TO_TICKS(100))) return;
    s_dexed->midiDataHandler(channel ? channel : 1, const_cast<uint8_t *>(data), static_cast<int16_t>(len));
    unlock_engine();
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
