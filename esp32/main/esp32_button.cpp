/*
 * ESP32 Button Input Handler
 *
 * Handles GPIO interrupt for the BOOT button (GPIO 0, active-low).
 * On press, toggles the SPX90 Symphonic effect.
 */

#include "esp32_button.h"
#include "esp32_config.h"
#include "esp32_audio.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "esp32_button";

// GPIO ISR service handle (shared across all GPIO)
static bool s_gpio_isr_service_installed = false;

// Debounce protection: track last press time
static volatile uint32_t s_last_press_time_ms = 0;
static const uint32_t DEBOUNCE_MS = 500;  // 500ms debounce

// ISR callback: GPIO-level interrupt handler (runs in ISR context)
static void IRAM_ATTR button_isr_handler(void *arg)
{
    (void)arg;
    uint32_t now = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;

    // Simple debounce: ignore if pressed within 500ms of last press
    if ((now - s_last_press_time_ms) > DEBOUNCE_MS) {
        s_last_press_time_ms = now;
        // Toggle symphonic effect (safe from ISR context)
        esp32_audio_toggle_symphonic();
    }
}

int esp32_button_init(void)
{
    ESP_LOGI(TAG, "Initializing BOOT button on GPIO %d", FMRACK_BOOT_BUTTON_PIN);

    // Install GPIO ISR service (must be done once per application)
    if (!s_gpio_isr_service_installed) {
        esp_err_t ret = gpio_install_isr_service(0);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
            return -1;
        }
        s_gpio_isr_service_installed = true;
    }

    // Configure GPIO 0 as input
    esp_err_t ret = gpio_set_direction((gpio_num_t)FMRACK_BOOT_BUTTON_PIN, GPIO_MODE_INPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO direction: %s", esp_err_to_name(ret));
        return -1;
    }

    // Enable internal pull-up (BOOT button is active-low / internally pulled high)
    ret = gpio_set_pull_mode((gpio_num_t)FMRACK_BOOT_BUTTON_PIN, GPIO_PULLUP_ONLY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO pull-up: %s", esp_err_to_name(ret));
        return -1;
    }

    // Attach interrupt handler on falling edge (button press = low)
    ret = gpio_isr_handler_add((gpio_num_t)FMRACK_BOOT_BUTTON_PIN, button_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add GPIO ISR handler: %s", esp_err_to_name(ret));
        return -1;
    }

    // Enable interrupt on falling edge
    ret = gpio_set_intr_type((gpio_num_t)FMRACK_BOOT_BUTTON_PIN, GPIO_INTR_NEGEDGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO interrupt type: %s", esp_err_to_name(ret));
        return -1;
    }

    ESP_LOGI(TAG, "BOOT button ready (GPIO %d, falling-edge triggered)", FMRACK_BOOT_BUTTON_PIN);
    return 0;
}

void esp32_button_deinit(void)
{
    gpio_isr_handler_remove((gpio_num_t)FMRACK_BOOT_BUTTON_PIN);
    ESP_LOGI(TAG, "Button handler deinitialized");
}
