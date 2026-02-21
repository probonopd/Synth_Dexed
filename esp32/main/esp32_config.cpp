/*
 * FMRack ESP32-S3 Port - Runtime Configuration
 *
 * Handles runtime configuration stored in NVS (Non-Volatile Storage).
 * This allows persisting settings like volume, selected performance,
 * and Wi-Fi credentials across reboots.
 */

#include "esp32_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "fmrack_config";

// NVS namespace
#define NVS_NAMESPACE "fmrack"

// Initialize NVS flash (required for Wi-Fi and persistent settings)
extern "C" int esp32_nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return -1;
    }
    ESP_LOGI(TAG, "NVS initialized");
    return 0;
}
