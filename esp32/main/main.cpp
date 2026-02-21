/*
 * FMRack ESP32-C5 Port - Main Application
 *
 * Complete port of FMRack (multi-timbral DX7 FM synthesizer rack) to the
 * ESP32-C5-DevKitC-1-N8R8 using ESP-IDF.
 *
 * Architecture:
 *   - Audio task (highest priority): Renders audio and feeds I2S DMA
 *   - MIDI UART task: Receives hardware MIDI (31250 baud)
 *   - MIDI USB task: Receives MIDI over USB CDC serial
 *   - UDP MIDI task: Receives MIDI over Wi-Fi
 *   - Status task: Blinks LED, monitors health
 *
 * Hardware connections:
 *   - I2S DAC (PCM5102A/MAX98357A): BCK=GPIO6, WS=GPIO7, DOUT=GPIO15
 *   - MIDI IN: UART1 RX=GPIO4 (via optocoupler)
 *   - Status LED: GPIO8
 */

#include "esp32_config.h"
#include "esp32_audio.h"
#include "esp32_midi.h"
#include "esp32_wifi.h"
#include "esp32_storage.h"
#include "fmrack_wrapper.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

static const char *TAG = "fmrack_main";

// Forward declaration
extern "C" int esp32_nvs_init(void);

// Status LED task
#if FMRACK_STATUS_LED_PIN >= 0
static void status_task(void *param)
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << FMRACK_STATUS_LED_PIN);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    bool led_state = false;
    int blink_count = 0;

    while (true) {
        if (fmrack_is_initialized() && esp32_audio_is_running()) {
            // Normal operation: slow heartbeat blink
            int active = fmrack_get_active_voices();
            if (active > 0) {
                // Fast blink when notes are playing
                led_state = !led_state;
                gpio_set_level((gpio_num_t)FMRACK_STATUS_LED_PIN, led_state);
                vTaskDelay(pdMS_TO_TICKS(50));
            } else {
                // Slow heartbeat
                led_state = (blink_count % 40) < 2;
                gpio_set_level((gpio_num_t)FMRACK_STATUS_LED_PIN, led_state);
                vTaskDelay(pdMS_TO_TICKS(50));
                blink_count++;
            }
        } else {
            // Not ready: rapid blink
            led_state = !led_state;
            gpio_set_level((gpio_num_t)FMRACK_STATUS_LED_PIN, led_state);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}
#endif

static void print_system_info(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  FMRack for ESP32-C5");
    ESP_LOGI(TAG, "  Multi-timbral DX7 FM Synthesizer");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Chip: %s (rev %d.%d), %d core(s)",
             CONFIG_IDF_TARGET,
             chip_info.revision / 100, chip_info.revision % 100,
             chip_info.cores);
    ESP_LOGI(TAG, "CPU freq: %d MHz", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "  Internal: %lu bytes",
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "  PSRAM: %lu bytes",
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "Config:");
    ESP_LOGI(TAG, "  Sample rate: %d Hz", FMRACK_SAMPLE_RATE);
    ESP_LOGI(TAG, "  Buffer size: %d samples", FMRACK_BUFFER_SIZE);
    ESP_LOGI(TAG, "  Modules: %d", FMRACK_NUM_MODULES);
    ESP_LOGI(TAG, "  I2S: BCK=%d WS=%d DOUT=%d",
             FMRACK_I2S_BCK_PIN, FMRACK_I2S_WS_PIN, FMRACK_I2S_DOUT_PIN);
    ESP_LOGI(TAG, "  MIDI: UART%d RX=%d",
             FMRACK_MIDI_UART_NUM, FMRACK_MIDI_RX_PIN);
#if FMRACK_MIDI_UDP_ENABLE
    ESP_LOGI(TAG, "  UDP MIDI: port %d", FMRACK_MIDI_UDP_PORT);
#endif
    ESP_LOGI(TAG, "========================================");
}

extern "C" void app_main(void)
{
    // Print system information
    print_system_info();

    // =====================
    // Phase 1: Core initialization
    // =====================
    ESP_LOGI(TAG, "[1/6] Initializing NVS...");
    if (esp32_nvs_init() != 0) {
        ESP_LOGE(TAG, "NVS initialization failed!");
        return;
    }

    // =====================
    // Phase 2: Storage (SPIFFS)
    // =====================
    ESP_LOGI(TAG, "[2/6] Initializing storage...");
    if (esp32_storage_init() != 0) {
        ESP_LOGW(TAG, "SPIFFS init failed (non-fatal, using defaults)");
        // Continue without storage - will use default performance
    }

    // =====================
    // Phase 3: FMRack synthesis engine
    // =====================
    ESP_LOGI(TAG, "[3/6] Initializing FMRack engine...");
    if (fmrack_init(FMRACK_SAMPLE_RATE, FMRACK_NUM_MODULES) != 0) {
        ESP_LOGE(TAG, "FMRack engine initialization failed!");
        return;
    }

    // Try to load default performance from SPIFFS
    if (esp32_storage_file_exists(FMRACK_DEFAULT_PERF)) {
        ESP_LOGI(TAG, "Loading default performance: %s", FMRACK_DEFAULT_PERF);
        fmrack_load_performance(FMRACK_DEFAULT_PERF);
    } else {
        ESP_LOGI(TAG, "No default performance file found, using INIT voice");
    }

    // =====================
    // Phase 4: Audio output (I2S)
    // =====================
    ESP_LOGI(TAG, "[4/6] Initializing audio output...");
    if (esp32_audio_init() != 0) {
        ESP_LOGE(TAG, "Audio initialization failed!");
        return;
    }

    if (esp32_audio_start() != 0) {
        ESP_LOGE(TAG, "Audio task start failed!");
        return;
    }

    // =====================
    // Phase 5: MIDI input
    // =====================
    ESP_LOGI(TAG, "[5/6] Initializing MIDI input...");
    if (esp32_midi_init() != 0) {
        ESP_LOGW(TAG, "MIDI UART init failed (non-fatal)");
    } else {
        esp32_midi_start();
    }

    // =====================
    // Phase 6: Wi-Fi and UDP MIDI
    // =====================
#if FMRACK_MIDI_UDP_ENABLE
    ESP_LOGI(TAG, "[6/6] Initializing Wi-Fi...");
    if (esp32_wifi_init() == 0) {
        esp32_wifi_udp_start();
    }
#else
    ESP_LOGI(TAG, "[6/6] Wi-Fi disabled in configuration");
#endif

    // =====================
    // Start status LED task
    // =====================
#if FMRACK_STATUS_LED_PIN >= 0
    xTaskCreate(status_task, "status", STATUS_TASK_STACK_SIZE,
                NULL, STATUS_TASK_PRIORITY, NULL);
#endif

    // =====================
    // System ready
    // =====================
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  FMRack is ready!");
    ESP_LOGI(TAG, "  Enabled parts: %d", fmrack_get_enabled_parts());
    ESP_LOGI(TAG, "  Send MIDI to start playing");
    ESP_LOGI(TAG, "========================================");

    // Print heap info after full initialization
    ESP_LOGI(TAG, "Post-init memory:");
    ESP_LOGI(TAG, "  Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "  Internal: %lu bytes",
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "  PSRAM: %lu bytes",
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "  Min free ever: %lu bytes",
             (unsigned long)esp_get_minimum_free_heap_size());

    // Main task can now sleep - all work is done in FreeRTOS tasks
    // Periodically log status
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(30000));  // Every 30 seconds

        ESP_LOGI(TAG, "Status: voices=%d parts=%d heap=%lu min_heap=%lu",
                 fmrack_get_active_voices(),
                 fmrack_get_enabled_parts(),
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)esp_get_minimum_free_heap_size());
    }
}
