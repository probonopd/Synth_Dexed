/*
 * FMRack ESP32-S3 Port - Main Application
 *
 * Complete port of FMRack (multi-timbral DX7 FM synthesizer rack) to the
 * ESP32-S3-DevKitC-1-N8R8 using ESP-IDF.
 *
 * Architecture (dual-core):
 *   Core 1 (dedicated):
 *     - Audio task (highest priority): Renders audio and feeds I2S DMA
 *   Core 0 (protocol):
 *     - MIDI UART task: Receives hardware MIDI (31250 baud)
 *     - USB Host MIDI task: Reads from attached USB-MIDI keyboards
 *     - UDP MIDI task: Receives MIDI over Wi-Fi (if enabled)
 *     - LED task: Animates the WS2812 status LED
 *
 * Hardware connections:
 *   - I2S DAC/Amp: MCLK=GPIO0, BCK=GPIO6, WS=GPIO7, DOUT=GPIO15
 *   - MIDI DIN: UART1 RX=GPIO18, TX=GPIO17
 *   - USB Host: GPIO19 (D-), GPIO20 (D+) — for USB-MIDI keyboards
 *   - Status LED: GPIO48 (WS2812 addressable RGB on DevKitC-1)
 */

#include "esp32_config.h"
#include "esp32_audio.h"
#include "esp32_midi.h"
#include "esp32_wifi.h"
#include "esp32_storage.h"
#include "esp32_led.h"
#include "fmrack_wrapper.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

static const char *TAG = "fmrack_main";

// Forward declaration
extern "C" int esp32_nvs_init(void);

/* ===================================================
 * Startup sound — three-note ascending chord (C4-E4-G4)
 * played tone-by-tone through the synth engine, then
 * all three notes released together.
 * =================================================== */
static void play_startup_sound(void)
{
    const uint8_t channel = 0;   // MIDI channel 1
    const uint8_t vel     = 80;

    // C4 (MIDI 60)
    fmrack_handle_midi(0x90 | channel, 60, vel);
    vTaskDelay(pdMS_TO_TICKS(180));

    // E4 (MIDI 64)
    fmrack_handle_midi(0x90 | channel, 64, vel);
    vTaskDelay(pdMS_TO_TICKS(180));

    // G4 (MIDI 67)
    fmrack_handle_midi(0x90 | channel, 67, vel);
    vTaskDelay(pdMS_TO_TICKS(400));

    // Release all three notes
    fmrack_handle_midi(0x80 | channel, 60, 0);
    fmrack_handle_midi(0x80 | channel, 64, 0);
    fmrack_handle_midi(0x80 | channel, 67, 0);

    // Let the release tail ring out
    vTaskDelay(pdMS_TO_TICKS(600));
}

/* ===================================================
 * Background task: update LED state based on system
 * status (USB keyboard connected, notes playing, etc.)
 * =================================================== */
static void led_monitor_task(void *param)
{
    while (true) {
        led_state_t current = esp32_led_get_state();

        /* Don't override boot-time states */
        if (current == LED_STATE_BOOTING ||
            current == LED_STATE_ENGINE_INIT ||
            current == LED_STATE_STARTUP_SOUND ||
            current == LED_STATE_ERROR) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Check for active voices (takes precedence) */
        if (fmrack_is_initialized() && fmrack_get_active_voices() > 0) {
            esp32_led_set_state(LED_STATE_PLAYING);
        }
        /* Check USB keyboard connection */
        else if (esp32_midi_usb_connected()) {
            esp32_led_set_state(LED_STATE_USB_CONNECTED);
        }
        /* Idle — ready */
        else {
            esp32_led_set_state(LED_STATE_READY);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void print_system_info(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  FMRack for ESP32-S3");
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
    ESP_LOGI(TAG, "  I2S: MCLK=%d BCK=%d WS=%d DOUT=%d",
             FMRACK_I2S_MCLK_PIN, FMRACK_I2S_BCK_PIN,
             FMRACK_I2S_WS_PIN, FMRACK_I2S_DOUT_PIN);
    ESP_LOGI(TAG, "  MIDI DIN: UART%d RX=%d TX=%d",
             FMRACK_MIDI_UART_NUM, FMRACK_MIDI_RX_PIN, FMRACK_MIDI_TX_PIN);
#if FMRACK_MIDI_USB_ENABLE
    ESP_LOGI(TAG, "  USB Host MIDI: enabled (GPIO19=D-, GPIO20=D+)");
#endif
#if FMRACK_MIDI_UDP_ENABLE
    ESP_LOGI(TAG, "  UDP MIDI: port %d", FMRACK_MIDI_UDP_PORT);
#else
    ESP_LOGI(TAG, "  UDP MIDI: disabled");
#endif
    ESP_LOGI(TAG, "========================================");
}

extern "C" void app_main(void)
{
    // =====================
    // Very first: init LED so we can show boot status
    // =====================
    if (esp32_led_init() == 0) {
        esp32_led_set_state(LED_STATE_BOOTING);
        esp32_led_start();
    }

    // Print system information
    print_system_info();

    // =====================
    // Phase 1: Core initialization
    // =====================
    ESP_LOGI(TAG, "[1/6] Initializing NVS...");
    if (esp32_nvs_init() != 0) {
        ESP_LOGE(TAG, "NVS initialization failed!");
        esp32_led_set_state(LED_STATE_ERROR);
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
    esp32_led_set_state(LED_STATE_ENGINE_INIT);

    if (fmrack_init(FMRACK_SAMPLE_RATE, FMRACK_NUM_MODULES) != 0) {
        ESP_LOGE(TAG, "FMRack engine initialization failed!");
        esp32_led_set_state(LED_STATE_ERROR);
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
        esp32_led_set_state(LED_STATE_ERROR);
        return;
    }

    if (esp32_audio_start() != 0) {
        ESP_LOGE(TAG, "Audio task start failed!");
        esp32_led_set_state(LED_STATE_ERROR);
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
    // Phase 6: Wi-Fi and UDP MIDI (disabled by default)
    // =====================
#if FMRACK_MIDI_UDP_ENABLE
    ESP_LOGI(TAG, "[6/6] Initializing Wi-Fi...");
    if (esp32_wifi_init() == 0) {
        esp32_wifi_udp_start();
    }
#else
    ESP_LOGI(TAG, "[6/6] Wi-Fi disabled (enable via menuconfig)");
#endif

    // =====================
    // Startup sound: C major chord (C4-E4-G4)
    // =====================
    ESP_LOGI(TAG, "Playing startup sound...");
    esp32_led_set_state(LED_STATE_STARTUP_SOUND);
    play_startup_sound();

    // =====================
    // System ready — switch LED to operational mode
    // =====================
    esp32_led_set_state(LED_STATE_READY);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  FMRack is ready!");
    ESP_LOGI(TAG, "  Enabled parts: %d", fmrack_get_enabled_parts());
    ESP_LOGI(TAG, "  Plug a USB-MIDI keyboard into the USB port");
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

    // =====================
    // Start LED monitor task (updates LED based on USB/voice state)
    // =====================
    xTaskCreatePinnedToCore(
        led_monitor_task,
        "led_mon",
        2048,
        NULL,
        STATUS_TASK_PRIORITY,
        NULL,
        0
    );

    // =====================
    // Periodic status logging
    // =====================
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "Status: voices=%d parts=%d usb=%s heap=%lu",
                 fmrack_get_active_voices(),
                 fmrack_get_enabled_parts(),
                 esp32_midi_usb_connected() ? "yes" : "no",
                 (unsigned long)esp_get_free_heap_size());
    }
}
