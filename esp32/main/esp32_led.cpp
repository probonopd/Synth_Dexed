/*
 * FMRack ESP32-S3 Port - RGB LED Status Indicator
 *
 * Drives the WS2812 addressable RGB LED on GPIO48 (ESP32-S3-DevKitC-1)
 * using the ESP-IDF RMT peripheral with a simple callback encoder.
 *
 * State → Color mapping:
 *   BOOTING        → solid red
 *   ENGINE_INIT    → solid yellow
 *   STARTUP_SOUND  → solid white (dim)
 *   READY          → breathing blue
 *   USB_CONNECTED  → solid green
 *   PLAYING        → pulsing cyan
 *   ERROR          → fast-blinking red
 *   OFF            → LED off
 */

#include "esp32_led.h"
#include "esp32_config.h"
#include "dexed_raw.h"
#include "esp32_midi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"

#include <string.h>
#include <math.h>

static const char *TAG = "dexed_led";

/* ── RMT / WS2812 constants ── */

#define RMT_LED_RESOLUTION_HZ  10000000  /* 10 MHz → 0.1 µs per tick */

/* WS2812 timing (in 0.1 µs ticks) */
static const rmt_symbol_word_t ws2812_bit0 = {
    .duration0 = 3,  .level0 = 1,   /* T0H = 0.3 µs */
    .duration1 = 9,  .level1 = 0,   /* T0L = 0.9 µs */
};
static const rmt_symbol_word_t ws2812_bit1 = {
    .duration0 = 9,  .level0 = 1,   /* T1H = 0.9 µs */
    .duration1 = 3,  .level1 = 0,   /* T1L = 0.3 µs */
};
static const rmt_symbol_word_t ws2812_reset = {
    .duration0 = 250, .level0 = 0,  /* 25 µs low */
    .duration1 = 250, .level1 = 0,  /* 25 µs low → total 50 µs reset */
};

/* Single-pixel GRB buffer (WS2812 order is G-R-B) */
static uint8_t s_pixel[3] = {0, 0, 0}; /* [G, R, B] */

static rmt_channel_handle_t s_led_chan = NULL;
static rmt_encoder_handle_t s_encoder = NULL;

static volatile led_state_t s_state = LED_STATE_OFF;
static volatile int s_fx_mode = 0; /* 0=both  1=reverb  2=symphonic  3=neither */
static TaskHandle_t s_led_task = NULL;

/* ── RMT simple encoder callback ── */

static size_t led_encoder_cb(const void *data, size_t data_size,
                              size_t symbols_written, size_t symbols_free,
                              rmt_symbol_word_t *symbols, bool *done, void *arg)
{
    if (symbols_free < 8) return 0;

    size_t byte_pos = symbols_written / 8;
    const uint8_t *bytes = (const uint8_t *)data;

    if (byte_pos < data_size) {
        size_t n = 0;
        uint8_t b = bytes[byte_pos];
        for (int mask = 0x80; mask; mask >>= 1) {
            symbols[n++] = (b & mask) ? ws2812_bit1 : ws2812_bit0;
        }
        return n;
    }

    /* All bytes sent → emit reset */
    symbols[0] = ws2812_reset;
    *done = true;
    return 1;
}

/* ── Send pixel data to the LED ── */

static void led_send(void)
{
    if (!s_led_chan || !s_encoder) return;

    rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
    rmt_transmit(s_led_chan, s_encoder, s_pixel, sizeof(s_pixel), &tx_cfg);
    rmt_tx_wait_all_done(s_led_chan, pdMS_TO_TICKS(50));
}

/* ── Public: set raw RGB color ── */

void esp32_led_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    /* WS2812 expects GRB order */
    s_pixel[0] = g;
    s_pixel[1] = r;
    s_pixel[2] = b;
    led_send();
}

/* ── LED animation task ── */

static void led_task(void *param)
{
    ESP_LOGI(TAG, "LED task started on core %d", xPortGetCoreID());

    int tick = 0;

    while (true) {
        led_state_t st = s_state;

        /* Auto-transition: if in READY, USB_CONNECTED or WLAN_ACTIVE,
         * upgrade to PLAYING when voices are active */
        if (st == LED_STATE_READY || st == LED_STATE_USB_CONNECTED ||
            st == LED_STATE_WLAN_ACTIVE) {
            if (dexed_raw_is_initialized() && dexed_raw_get_active_voices() > 0) {
                st = LED_STATE_PLAYING;
            }
        }

        switch (st) {
            case LED_STATE_OFF:
                esp32_led_set_rgb(0, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            case LED_STATE_BOOTING:
                /* Solid red */
                esp32_led_set_rgb(40, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            case LED_STATE_ENGINE_INIT:
                /* Solid yellow */
                esp32_led_set_rgb(40, 25, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            case LED_STATE_STARTUP_SOUND:
                /* Soft white */
                esp32_led_set_rgb(30, 30, 30);
                vTaskDelay(pdMS_TO_TICKS(50));
                break;

            case LED_STATE_READY: {
                /* Breathing animation — color depends on active effects mode */
                float breath = (sinf((float)tick * 0.05f) + 1.0f) * 0.5f;
                float scale = 0.1f + breath * 0.7f;
                switch (s_fx_mode) {
                    case 1: /* reverb only — purple */
                        esp32_led_set_rgb((uint8_t)(20*scale), 0, (uint8_t)(40*scale)); break;
                    case 2: /* symphonic only — amber */
                        esp32_led_set_rgb((uint8_t)(40*scale), (uint8_t)(15*scale), 0); break;
                    case 3: /* neither — cool white */
                        esp32_led_set_rgb((uint8_t)(20*scale), (uint8_t)(20*scale), (uint8_t)(20*scale)); break;
                    default: /* both active — cyan */
                        esp32_led_set_rgb(0, (uint8_t)(40*scale), (uint8_t)(40*scale)); break;
                }
                vTaskDelay(pdMS_TO_TICKS(30));
                tick++;
                break;
            }

            case LED_STATE_USB_CONNECTED: {
                /* Solid dim fx_mode color */
                float scale = 0.45f;
                switch (s_fx_mode) {
                    case 1: /* reverb only — purple */
                        esp32_led_set_rgb((uint8_t)(20*scale), 0, (uint8_t)(40*scale)); break;
                    case 2: /* symphonic only — amber */
                        esp32_led_set_rgb((uint8_t)(40*scale), (uint8_t)(15*scale), 0); break;
                    case 3: /* neither — cool white */
                        esp32_led_set_rgb((uint8_t)(20*scale), (uint8_t)(20*scale), (uint8_t)(20*scale)); break;
                    default: /* both active — cyan */
                        esp32_led_set_rgb(0, (uint8_t)(40*scale), (uint8_t)(40*scale)); break;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
            }

            case LED_STATE_PLAYING: {
                /* Pulsing fx_mode color — faster pulse than breathing */
                float pulse = (sinf((float)tick * 0.15f) + 1.0f) * 0.5f;
                float scale = 0.2f + pulse * 0.8f;
                switch (s_fx_mode) {
                    case 1: /* reverb only — purple */
                        esp32_led_set_rgb((uint8_t)(20*scale), 0, (uint8_t)(40*scale)); break;
                    case 2: /* symphonic only — amber */
                        esp32_led_set_rgb((uint8_t)(40*scale), (uint8_t)(15*scale), 0); break;
                    case 3: /* neither — cool white */
                        esp32_led_set_rgb((uint8_t)(20*scale), (uint8_t)(20*scale), (uint8_t)(20*scale)); break;
                    default: /* both active — cyan */
                        esp32_led_set_rgb(0, (uint8_t)(40*scale), (uint8_t)(40*scale)); break;
                }
                vTaskDelay(pdMS_TO_TICKS(20));
                tick++;
                break;
            }

            case LED_STATE_WLAN_ACTIVE: {
                /* Pulsing orange — WLAN mode active, no notes playing */
                float pulse = (sinf((float)tick * 0.08f) + 1.0f) * 0.5f;
                float scale = 0.15f + pulse * 0.85f;
                esp32_led_set_rgb((uint8_t)(50*scale), (uint8_t)(18*scale), 0);
                vTaskDelay(pdMS_TO_TICKS(25));
                tick++;
                break;
            }

            case LED_STATE_ERROR:
                /* Fast-blinking red */
                esp32_led_set_rgb((tick & 1) ? 50 : 0, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(150));
                tick++;
                break;
        }
    }
}

/* ── Public API ── */

int esp32_led_init(void)
{
#if FMRACK_STATUS_LED_PIN < 0
    ESP_LOGW(TAG, "Status LED disabled (pin < 0)");
    return -1;
#else
    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num          = (gpio_num_t)FMRACK_STATUS_LED_PIN,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = RMT_LED_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    esp_err_t err = rmt_new_tx_channel(&tx_cfg, &s_led_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RMT TX channel failed: %s", esp_err_to_name(err));
        return -1;
    }

    rmt_simple_encoder_config_t enc_cfg = {
        .callback = led_encoder_cb,
    };
    err = rmt_new_simple_encoder(&enc_cfg, &s_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RMT encoder failed: %s", esp_err_to_name(err));
        return -1;
    }

    err = rmt_enable(s_led_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RMT enable failed: %s", esp_err_to_name(err));
        return -1;
    }

    /* Start with LED off */
    esp32_led_set_rgb(0, 0, 0);

    ESP_LOGI(TAG, "WS2812 LED initialized on GPIO%d", FMRACK_STATUS_LED_PIN);
    return 0;
#endif
}

void esp32_led_set_state(led_state_t state)
{
    s_state = state;
}

led_state_t esp32_led_get_state(void)
{
    return s_state;
}

void esp32_led_set_fx_mode(int mode)
{
    s_fx_mode = mode;
}

void esp32_led_start(void)
{
    if (s_led_task) return;

    xTaskCreatePinnedToCore(
        led_task,
        "led",
        STATUS_TASK_STACK_SIZE,
        NULL,
        STATUS_TASK_PRIORITY,
        &s_led_task,
        0  /* Core 0 — protocol core */
    );
}
