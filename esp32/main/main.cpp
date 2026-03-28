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
 *   - I2S DAC/Amp: MCLK=GPIO1, BCK=GPIO5, WS=GPIO6, DOUT=GPIO7
 *   - MIDI DIN: UART1 RX=GPIO18, TX=GPIO17
 *   - USB Host: GPIO19 (D-), GPIO20 (D+) — for USB-MIDI keyboards
 *   - Status LED: GPIO48 (WS2812 addressable RGB on DevKitC-1)
 *   - BOOT Button: GPIO0 (active-low, toggle symphonic effect)
 */

#include "esp32_config.h"
#include "esp32_audio.h"
#include "esp32_midi.h"
#include "esp32_wifi.h"
#include "esp32_applemidi.h"
#include "esp32_storage.h"
#include "esp32_led.h"
#include "esp32_usb_audio.h"
#include "esp32_button.h"
#include "dexed_raw.h"
#include "tsf_engine.h"
#include "step_sequencer.h"
#include "launchpad.h"
#include "esp32_oled.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

static const char *TAG = "dexed_main";

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
    const uint8_t vel     = 72;

    // Longer deterministic boot melody (no external MIDI required).
    // Simple 8th-note line in C major with a short held chord at the end.
    static const uint8_t melody[] = {
        60, 62, 64, 67, 69, 67, 64, 62,
        60, 62, 64, 67, 72, 71, 69, 67,
        64, 62, 60, 62, 64, 67, 69, 72,
        71, 69, 67, 64, 62, 60,
    };

    for (size_t i = 0; i < sizeof(melody); ++i) {
        const uint8_t note = melody[i];
        dexed_raw_handle_midi(0x90 | channel, note, vel);
        vTaskDelay(pdMS_TO_TICKS(140));
        dexed_raw_handle_midi(0x80 | channel, note, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // End with a short C major chord
    dexed_raw_handle_midi(0x90 | channel, 60, vel);
    dexed_raw_handle_midi(0x90 | channel, 64, vel);
    dexed_raw_handle_midi(0x90 | channel, 67, vel);
    vTaskDelay(pdMS_TO_TICKS(700));
    dexed_raw_handle_midi(0x80 | channel, 60, 0);
    dexed_raw_handle_midi(0x80 | channel, 64, 0);
    dexed_raw_handle_midi(0x80 | channel, 67, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
}

/* ===================================================
 * Background task: update LED state based on system
 * status (USB keyboard connected, notes playing, etc.)
 * =================================================== */
static void led_monitor_task(void *param)
{
    /* TSF load progress: only update Launchpad while loading is in-progress.
     * Once loading ends (success=8 or failure<0) we show the result ONCE and
     * hand the Launchpad back to the sequencer's refresh logic.
     *
     * On failure, the sequence is:
     *   1. Show step-indicator (which step turned red) for 3 s
     *   2. Show binary error code permanently until next grid refresh
     */
    bool tsf_load_done = false;
    bool tsf_error_shown = false;        /* true once binary code is on screen */
    int  tsf_error_step_shown_ms = 0;   /* ms ticker for the 3-second pause */

    while (true) {
        led_state_t current = esp32_led_get_state();

        /* Drive Launchpad loading progress bar while TSF is loading */
        if (!tsf_load_done) {
            int tsf_prog = tsf_engine_get_load_progress();

            if (tsf_prog > 0 && tsf_prog < 8) {
                /* Loading in progress — show green/amber progress bar */
                if (launchpad_is_connected()) {
                    launchpad_show_loading_progress(tsf_prog);
                }
            } else if (tsf_prog == 8) {
                /* Fully loaded — all green on right column, then hand off */
                if (launchpad_is_connected()) {
                    launchpad_show_loading_progress(8);
                }
                tsf_load_done = true;
            } else if (tsf_prog < 0) {
                if (!tsf_error_shown) {
                    if (tsf_error_step_shown_ms == 0) {
                        /* First time: show which step failed (red LED) */
                        if (launchpad_is_connected()) {
                            launchpad_show_loading_progress(tsf_prog);
                        }
                        ESP_LOGW(TAG, "[tsf] Load failed at step %d, error_detail=0x%02X",
                                 -tsf_prog, (unsigned)tsf_engine_get_error_detail());
                    }
                    tsf_error_step_shown_ms += 100;

                    if (tsf_error_step_shown_ms >= 3000) {
                        /* After 3 s: switch to binary error code display */
                        tsf_error_shown = true;
                        uint8_t detail = tsf_engine_get_error_detail();
                        if (launchpad_is_connected() && detail != 0) {
                            launchpad_show_tsf_error(detail);
                        } else if (launchpad_is_connected()) {
                            /* No detail flags (e.g. file not found before decode):
                             * keep the step-indicator on screen */
                            launchpad_show_loading_progress(tsf_prog);
                        }
                        tsf_load_done = true;
                    }
                }
            }
        }

        /* Don't override boot-time states */
        if (current == LED_STATE_BOOTING ||
            current == LED_STATE_ENGINE_INIT ||
            current == LED_STATE_STARTUP_SOUND ||
            current == LED_STATE_ERROR) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Check for active voices (takes precedence over everything) */
        if (dexed_raw_is_initialized() && dexed_raw_get_active_voices() > 0) {
            esp32_led_set_state(LED_STATE_PLAYING);
        }
        /* WLAN mode active (or connecting) — orange pulse */
        else if (esp32_button_is_wlan_mode()) {
            esp32_led_set_state(LED_STATE_WLAN_ACTIVE);
        }
        /* USB MIDI keyboard connected */
        else if (esp32_midi_usb_connected()) {
            esp32_led_set_state(LED_STATE_USB_CONNECTED);
        }
        /* Idle — ready, synth mode */
        else {
            esp32_led_set_state(LED_STATE_READY);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ===================================================
 * Background task: load TSF SoundFont
 * Runs on Core 0 at low priority so it doesn't affect audio.
 * Launchpad shows progress via led_monitor_task polling.
 * =================================================== */
static void tsf_load_task(void *param)
{
    ESP_LOGI(TAG, "[tsf] Background load task started");
    if (tsf_engine_init((int)(intptr_t)param) != 0) {
        ESP_LOGW(TAG, "[tsf] Load task: init failed (drums use Dexed FM)");
    }
    vTaskDelete(NULL);
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
    ESP_LOGI(TAG, "  Engine: raw Dexed (single instance)");
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
    /* Log the reason for the previous reset so we can diagnose boot loops. */
    {
        esp_reset_reason_t reason = esp_reset_reason();
        const char *reason_str = "UNKNOWN";
        switch (reason) {
            case ESP_RST_POWERON:  reason_str = "POWERON";  break;
            case ESP_RST_EXT:      reason_str = "EXT_RESET"; break;
            case ESP_RST_SW:       reason_str = "SW_RESET";  break;
            case ESP_RST_PANIC:    reason_str = "PANIC";     break;
            case ESP_RST_INT_WDT:  reason_str = "INT_WDT";  break;
            case ESP_RST_TASK_WDT: reason_str = "TASK_WDT";  break;
            case ESP_RST_WDT:      reason_str = "WDT_OTHER"; break;
            case ESP_RST_DEEPSLEEP:reason_str = "DEEPSLEEP"; break;
            case ESP_RST_BROWNOUT: reason_str = "BROWNOUT";  break;
            case ESP_RST_SDIO:     reason_str = "SDIO";      break;
            case ESP_RST_USB:      reason_str = "USB";       break;
            default: break;
        }
        ESP_LOGW(TAG, "*** RESET REASON: %s (%d) ***", reason_str, (int)reason);
    }

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
    // Phase 3: Dexed synthesis engine
    // =====================
    ESP_LOGI(TAG, "[3/6] Initializing Dexed engine...");
    esp32_led_set_state(LED_STATE_ENGINE_INIT);

    if (dexed_raw_init(FMRACK_SAMPLE_RATE) != 0) {
        ESP_LOGE(TAG, "Dexed engine initialization failed!");
        esp32_led_set_state(LED_STATE_ERROR);
        return;
    }

    // Preferred: load DX7 factory bank from SPIFFS and load E.PIANO 1 directly.
    // Put a DX7 factory bank at: /spiffs/rom1a.syx
    // E.PIANO 1 is program 11 (1-based), so index0=10.
    const char *rom1a_path = "/spiffs/rom1a.syx";
    if (esp32_storage_file_exists(rom1a_path)) {
        ESP_LOGI(TAG, "Loading ROM1A voice 11 (E.PIANO 1) from: %s", rom1a_path);
        if (dexed_raw_load_bank_syx(rom1a_path) == 0 && dexed_raw_select_bank_program(10) == 0) {
            // ok
        } else {
            ESP_LOGW(TAG, "ROM1A load failed, falling back to %s", FMRACK_DEFAULT_PERF);
            if (esp32_storage_file_exists(FMRACK_DEFAULT_PERF)) {
                (void)dexed_raw_load_voice_from_performance_ini(FMRACK_DEFAULT_PERF);
            }
        }
    } else if (esp32_storage_file_exists(FMRACK_DEFAULT_PERF)) {
        // Fallback: load VoiceData1 from SPIFFS performance.ini
        ESP_LOGI(TAG, "Loading voice from: %s", FMRACK_DEFAULT_PERF);
        (void)dexed_raw_load_voice_from_performance_ini(FMRACK_DEFAULT_PERF);
    }

    // TSF drum engine: launched as a background task after step_seq_init()
    // so the Launchpad is available to show loading progress.

    // Initialize step sequencer (engine only; Launchpad UI auto-starts on detect)
    if (step_seq_init() != 0) {
        ESP_LOGW(TAG, "Step sequencer init failed (non-fatal)");
    }

    // OLED display (non-fatal if not connected)
    esp32_oled_init();

    // Launch TSF drum engine loader as a background task.
    // MIDI (USB host) is started AFTER TSF completes to prevent concurrent
    // PSRAM allocations from fragmenting the heap during the OGG decode.
    // The LED monitor task shows Launchpad progress during the wait.
    ESP_LOGI(TAG, "Launching TSF drum engine background loader...");
    xTaskCreatePinnedToCore(
        tsf_load_task,
        "tsf_load",
        16384,
        (void *)(intptr_t)FMRACK_SAMPLE_RATE,
        tskIDLE_PRIORITY + 1,
        NULL,
        0   /* Core 0, away from audio task on Core 1 */
    );

    // Start LED monitor task now so it can drive the Launchpad
    // loading progress bar while TSF is decoding in the background.
    xTaskCreatePinnedToCore(
        led_monitor_task,
        "led_mon",
        2048,
        NULL,
        STATUS_TASK_PRIORITY,
        NULL,
        0
    );

    // Wait for TSF to finish before starting USB host (MIDI).
    // USB host allocates PSRAM buffers for device enumeration; those
    // allocations fragment the heap and break the float-buffer realloc
    // chain inside tsf_decode_ogg even when total free PSRAM is sufficient.
    // TSF decode takes ~2 seconds; app_main blocks here (LED monitor still
    // runs in its own task and drives the Launchpad progress bar).
    {
        int prog;
        // Phase 1: wait until the TSF task actually starts (prog transitions
        // from 0 to positive; the task may not have run yet when we check).
        for (int i = 0; i < 100 && tsf_engine_get_load_progress() == 0; i++) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        // Phase 2: wait until TSF finishes (prog=8 success, or prog<0 failure)
        while ((prog = tsf_engine_get_load_progress()) > 0 && prog < 8) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        ESP_LOGI(TAG, "TSF load complete (prog=%d)", prog);
    }

    // =====================
    // Phase 4: MIDI input (start now that TSF has exclusive PSRAM)
    // =====================
    ESP_LOGI(TAG, "[4/6] Initializing MIDI input (UART + USB host)...");
    if (esp32_midi_init() != 0) {
        ESP_LOGW(TAG, "MIDI UART init failed (non-fatal)");
    } else {
        esp32_midi_start();
    }

    // Pause to let USB host settle.  The hub + keyboard need time to
    // enumerate; the USB ENUM component may encounter CHECK_SHORT_DEV_DESC
    // on first attempt and retry -- that retry typically takes 500-800 ms.
    // 2 seconds gives enough headroom before I2S DMA starts.
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Register USB audio client after the settle delay.
    if (esp32_usb_audio_init() != 0) {
        ESP_LOGW(TAG, "USB audio init failed (non-fatal, continuing without USB audio)");
    }

    // =====================
    // Phase 5: Audio output (I2S)
    // =====================
    ESP_LOGI(TAG, "[5/6] Initializing audio output...");
    if (esp32_audio_init() != 0) {
        ESP_LOGW(TAG, "Audio initialization returned error, continuing anyway");
        // do not return; keep app_main alive so the i2s_init task can notify
        // and so we don't trigger the notification crash seen earlier.
        // The audio driver may still work even if init reported failure.
    }

    if (esp32_audio_start() != 0) {
        ESP_LOGE(TAG, "Audio task start failed!");
        esp32_led_set_state(LED_STATE_ERROR);
        return;
    }

    // Initialize BOOT button handler (GPIO 0, toggle symphonic effect)
    if (esp32_button_init() != 0) {
        ESP_LOGW(TAG, "Button initialization failed (non-fatal)");
    }

    // =====================
    // Phase 6: WLAN, Apple MIDI, and captive portal
    //   - Loads stored credentials and connects (STA mode); or
    //   - Falls back to captive AP "Synth-Dexed-Setup" for first-time setup.
    //   On a successful STA connection Apple MIDI + mDNS are started
    //   automatically so the synth appears in Audio MIDI Setup on the Mac.
    // =====================
    ESP_LOGI(TAG, "[6/6] WLAN deferred — long-press BOOT button to enable WLAN mode");

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
    ESP_LOGI(TAG, "  Dexed is ready!");
    ESP_LOGI(TAG, "  Enabled parts: 1");
    ESP_LOGI(TAG, "  Plug a USB-MIDI keyboard into the USB port");
    if (esp32_wlan_is_connected()) {
        ESP_LOGI(TAG, "  WLAN: connected — Apple MIDI active");
        ESP_LOGI(TAG, "  Open Audio MIDI Setup on Mac → Network → '%s'",
                 FMRACK_APPLEMIDI_NAME);
    } else if (esp32_wlan_is_ap_mode()) {
        ESP_LOGI(TAG, "  WLAN: captive AP active — connect to '%s'",
                 FMRACK_CAPTIVE_AP_SSID);
    }
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
    // Periodic status logging
    // =====================
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "Status: voices=%d tsf=%s(%d) usb-midi=%s%s usb-audio=%s(ch=%d) wlan=%s apple-midi=%s seq=%s bpm=%d heap=%lu",
                 dexed_raw_get_active_voices(),
                 tsf_engine_is_loaded() ? "loaded" : "off",
                 tsf_engine_active_voices(),
                 esp32_midi_usb_connected() ? "yes" : "no",
                 esp32_midi_is_launchpad() ? "(LP)" : "",
                 esp32_usb_audio_is_ready() ? "yes" : "no",
                 esp32_usb_audio_get_channels(),
                 esp32_wlan_is_connected() ? "sta" :
                     (esp32_wlan_is_ap_mode() ? "ap" : "off"),
                 esp32_applemidi_is_connected() ? "yes" : "no",
                 step_seq_is_playing() ? "playing" : "stopped",
                 step_seq_get_bpm(),
                 (unsigned long)esp_get_free_heap_size());
    }
}
