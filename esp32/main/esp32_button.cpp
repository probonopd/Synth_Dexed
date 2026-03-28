/*
 * ESP32 Button Input Handler
 *
 * BOOT button (GPIO 0, active-low):
 *   Short press (< 1500 ms) → cycle through 4 effects modes
 *   Long  press (≥ 1500 ms) → toggle WLAN ↔ synth-only mode
 *
 * WLAN and synth/music mode are mutually exclusive by design:
 *   - Default at boot: WLAN OFF (all resources for audio)
 *   - Long press starts WLAN (Apple MIDI, captive portal, etc.)
 *   - Another long press stops WLAN and returns to synth mode
 *
 * ANYEDGE interrupt: falling = pressed, rising = released.
 * Duration computed on release; events queued to a handler task so that
 * blocking WLAN operations (wlan_init / wlan_stop) run outside ISR context.
 */

#include "esp32_button.h"
#include "esp32_config.h"
#include "esp32_audio.h"
#include "esp32_oled.h"
#include "esp32_wifi.h"
#include "esp32_led.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "esp32_button";

/* ── Timing thresholds ── */
#define LONG_PRESS_MS      1500u   /* ≥ 1.5 s → WLAN toggle          */
#define SHORT_PRESS_MIN_MS   50u   /* < 50 ms → ignored (bounce)      */

/* ── Button event codes sent from ISR → handler task ── */
#define BTN_EVENT_SHORT 0u
#define BTN_EVENT_LONG  1u

/* ── State ── */
static bool s_gpio_isr_service_installed = false;
static volatile uint32_t s_press_time_ms = 0;   /* time of last falling edge */
static volatile bool     s_pressed       = false;
static QueueHandle_t     s_event_queue   = NULL;
static volatile bool     s_wlan_active   = false; /* tracks WLAN on/off       */

/* ── ISR: fires on both edges ── */
static void IRAM_ATTR button_isr_handler(void *arg)
{
    (void)arg;
    int level = gpio_get_level((gpio_num_t)FMRACK_BOOT_BUTTON_PIN);
    uint32_t now = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;

    if (level == 0) {
        /* Falling edge: button pressed — record timestamp */
        s_press_time_ms = now;
        s_pressed = true;
    } else {
        /* Rising edge: button released — classify and enqueue */
        if (!s_pressed) return;
        s_pressed = false;
        uint32_t duration = now - s_press_time_ms;

        uint8_t event;
        if (duration >= LONG_PRESS_MS) {
            event = BTN_EVENT_LONG;
        } else if (duration >= SHORT_PRESS_MIN_MS) {
            event = BTN_EVENT_SHORT;
        } else {
            return; /* noise / contact bounce */
        }

        BaseType_t hp_woken = pdFALSE;
        xQueueSendFromISR(s_event_queue, &event, &hp_woken);
        if (hp_woken) portYIELD_FROM_ISR();
    }
}

/* ── Handler task: executes actions outside ISR context ── */
static void button_task(void *param)
{
    (void)param;
    uint8_t event;
    while (true) {
        if (xQueueReceive(s_event_queue, &event, portMAX_DELAY) != pdTRUE) continue;

        if (event == BTN_EVENT_SHORT) {
            /* Short press: advance through the 4 effects modes */
            esp32_audio_cycle_effects();
            esp32_oled_show_fx(esp32_audio_get_fx_mode());

        } else if (event == BTN_EVENT_LONG) {
            if (!s_wlan_active) {
                /* ── Enter WLAN mode ── */
                ESP_LOGI(TAG, "Long press: WLAN mode ON (synth audio continues)");
                esp32_oled_show_wlan(true);
                esp32_led_set_state(LED_STATE_WLAN_ACTIVE);
                esp32_wlan_init();          /* blocking: connects or starts AP */
                s_wlan_active = true;
            } else {
                /* ── Return to synth-only mode ── */
                ESP_LOGI(TAG, "Long press: WLAN mode OFF → synth-only mode");
                esp32_oled_show_wlan(false);
                esp32_wlan_stop();
                s_wlan_active = false;
                esp32_led_set_state(LED_STATE_READY);
            }
        }
    }
}

/* ── Public API ── */

int esp32_button_init(void)
{
    ESP_LOGI(TAG, "Initializing BOOT button on GPIO %d", FMRACK_BOOT_BUTTON_PIN);

    s_event_queue = xQueueCreate(4, sizeof(uint8_t));
    if (!s_event_queue) {
        ESP_LOGE(TAG, "Failed to create button event queue");
        return -1;
    }

    /* Button task on Core 0 (protocol core) — WLAN ops must not run on Core 1 */
    BaseType_t ok = xTaskCreatePinnedToCore(
        button_task, "btn_task", 4096, NULL,
        STATUS_TASK_PRIORITY + 1, NULL, 0);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task");
        return -1;
    }

    /* Install GPIO ISR service (shared; safe to call if already installed) */
    if (!s_gpio_isr_service_installed) {
        esp_err_t ret = gpio_install_isr_service(0);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
            return -1;
        }
        s_gpio_isr_service_installed = true;
    }

    esp_err_t ret = gpio_set_direction((gpio_num_t)FMRACK_BOOT_BUTTON_PIN, GPIO_MODE_INPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO direction: %s", esp_err_to_name(ret));
        return -1;
    }

    ret = gpio_set_pull_mode((gpio_num_t)FMRACK_BOOT_BUTTON_PIN, GPIO_PULLUP_ONLY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO pull-up: %s", esp_err_to_name(ret));
        return -1;
    }

    ret = gpio_isr_handler_add((gpio_num_t)FMRACK_BOOT_BUTTON_PIN, button_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add GPIO ISR handler: %s", esp_err_to_name(ret));
        return -1;
    }

    /* ANYEDGE: need both press and release to measure duration */
    ret = gpio_set_intr_type((gpio_num_t)FMRACK_BOOT_BUTTON_PIN, GPIO_INTR_ANYEDGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO interrupt type: %s", esp_err_to_name(ret));
        return -1;
    }

    ESP_LOGI(TAG, "BOOT button ready — short=cycle-fx  long(1.5s)=toggle-WLAN");
    return 0;
}

void esp32_button_deinit(void)
{
    gpio_isr_handler_remove((gpio_num_t)FMRACK_BOOT_BUTTON_PIN);
    ESP_LOGI(TAG, "Button handler deinitialized");
}

bool esp32_button_is_wlan_mode(void)
{
    return s_wlan_active;
}
