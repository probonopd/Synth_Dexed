/*
 * FMRack ESP32-S3 Port - Engine Wrapper Implementation
 *
 * Bridges the ESP32-S3 platform code to the C++ FMRack engine.
 * All FMRack C++ objects are managed here.
 *
 * Key adaptations for ESP32-S3:
 * - Rack and modules are allocated in PSRAM via placement new
 * - Multiprocessing enabled (ESP32-S3 is dual-core Xtensa LX7)
 * - std::filesystem calls are replaced with POSIX file ops
 * - 8 modules by default (dual-core + 240 MHz headroom)
 */

#include "fmrack_wrapper.h"
#include "esp32_config.h"

// FMRack engine headers
#include "Rack.h"
#include "Performance.h"
#include "VoiceData.h"
#include "Debug.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <memory>
#include <string>
#include <cstring>

static const char *TAG = "fmrack_engine";

// Global debug flag (required by FMRack Debug.h)
bool debugEnabled = false;

// Multiprocessing flag (required by FMRack Debug.h)
// Disabled: the engine's internal std::thread-based multiprocessing is not
// suitable for FreeRTOS.  We achieve dual-core operation by pinning the
// audio task to core 1 and protocol tasks to core 0 instead.
int multiprocessingEnabled = 0;

// The FMRack engine instance
static FMRack::Rack *s_rack = nullptr;

// Mutex for thread-safe MIDI message handling
static SemaphoreHandle_t s_midi_mutex = NULL;

// Performance directory for program changes
static std::string s_performance_dir;

int fmrack_init(int sample_rate, int num_modules)
{
    ESP_LOGI(TAG, "Initializing FMRack engine...");
    ESP_LOGI(TAG, "  Sample rate: %d Hz", sample_rate);
    ESP_LOGI(TAG, "  Modules: %d", num_modules);

    // Create MIDI mutex
    s_midi_mutex = xSemaphoreCreateMutex();
    if (!s_midi_mutex) {
        ESP_LOGE(TAG, "Failed to create MIDI mutex");
        return -1;
    }

    // Allocate Rack in PSRAM
    void *rack_mem = heap_caps_malloc(sizeof(FMRack::Rack), MALLOC_CAP_SPIRAM);
    if (!rack_mem) {
        ESP_LOGW(TAG, "PSRAM allocation failed for Rack, using internal memory");
        rack_mem = malloc(sizeof(FMRack::Rack));
        if (!rack_mem) {
            ESP_LOGE(TAG, "Failed to allocate Rack");
            return -1;
        }
    }

    // Construct Rack in-place
    s_rack = new (rack_mem) FMRack::Rack(static_cast<float>(sample_rate));

    if (!s_rack->isInitialized()) {
        ESP_LOGE(TAG, "Rack initialization failed");
        s_rack->~Rack();
        heap_caps_free(rack_mem);
        s_rack = nullptr;
        return -1;
    }

    // Set up default performance with configured number of modules
    FMRack::Performance perf;
    perf.setDefaults(num_modules, 1);  // 1 unison voice per module (ESP32 CPU limit)
    for (int i = 0; i < 16; i++) {
        if (i < num_modules) {
            perf.parts[i].midiChannel = static_cast<uint8_t>(i + 1);
            perf.parts[i].volume = 100;
            perf.parts[i].unisonVoices = 1;  // No unison on ESP32 to save CPU
        } else {
            perf.parts[i].midiChannel = 0;
        }
    }
    s_rack->setPerformance(perf);

    ESP_LOGI(TAG, "FMRack engine initialized with %d modules", num_modules);
    ESP_LOGI(TAG, "  Free heap: %lu bytes (internal: %lu, PSRAM: %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    return 0;
}

void fmrack_deinit(void)
{
    if (s_rack) {
        s_rack->~Rack();
        heap_caps_free(s_rack);
        s_rack = nullptr;
    }

    if (s_midi_mutex) {
        vSemaphoreDelete(s_midi_mutex);
        s_midi_mutex = NULL;
    }

    ESP_LOGI(TAG, "FMRack engine deinitialized");
}

int fmrack_load_performance(const char *path)
{
    if (!s_rack || !path) return -1;

    ESP_LOGI(TAG, "Loading performance: %s", path);

    if (s_rack->loadPerformance(std::string(path))) {
        // Store the directory for program changes
        std::string pathStr(path);
        size_t lastSlash = pathStr.find_last_of('/');
        if (lastSlash != std::string::npos) {
            s_performance_dir = pathStr.substr(0, lastSlash);
        }
        ESP_LOGI(TAG, "Performance loaded: %d enabled parts",
                 s_rack->getEnabledPartCount());
        return 0;
    }

    ESP_LOGE(TAG, "Failed to load performance: %s", path);
    return -1;
}

void fmrack_process_audio(float *left_out, float *right_out, int num_samples)
{
    if (s_rack) {
        s_rack->processAudio(left_out, right_out, num_samples);
    } else {
        memset(left_out, 0, num_samples * sizeof(float));
        memset(right_out, 0, num_samples * sizeof(float));
    }
}

void fmrack_handle_midi(uint8_t status, uint8_t data1, uint8_t data2)
{
    if (!s_rack) return;

    // Check for program change
    if ((status & 0xF0) == 0xC0 && !s_performance_dir.empty()) {
        fmrack_handle_program_change(data1, s_performance_dir.c_str());
        return;
    }

    // Thread-safe MIDI routing
    if (xSemaphoreTake(s_midi_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        s_rack->processMidiMessage(status, data1, data2);
        xSemaphoreGive(s_midi_mutex);
    }
}

void fmrack_handle_sysex(const uint8_t *data, int len, uint8_t channel)
{
    if (!s_rack || !data || len < 2) return;

    if (xSemaphoreTake(s_midi_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_rack->routeSysexToModules(data, len, channel);
        xSemaphoreGive(s_midi_mutex);
    }
}

void fmrack_handle_program_change(int program, const char *dir)
{
    if (!s_rack || !dir) return;

    if (xSemaphoreTake(s_midi_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_rack->handleProgramChange(program, std::string(dir));
        xSemaphoreGive(s_midi_mutex);
    }
}

int fmrack_get_active_voices(void)
{
    return s_rack ? s_rack->getActiveVoices() : 0;
}

int fmrack_get_enabled_parts(void)
{
    return s_rack ? s_rack->getEnabledPartCount() : 0;
}

bool fmrack_is_initialized(void)
{
    return s_rack != nullptr && s_rack->isInitialized();
}
