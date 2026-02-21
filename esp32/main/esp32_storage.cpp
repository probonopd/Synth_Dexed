/*
 * FMRack ESP32-S3 Port - SPIFFS Storage Implementation
 *
 * Mounts the SPIFFS partition for storing performance files,
 * DX7 voice banks (.syx), and configuration data.
 */

#include "esp32_storage.h"
#include "esp32_config.h"

#include "esp_spiffs.h"
#include "esp_log.h"

#include <sys/stat.h>
#include <string.h>

static const char *TAG = "fmrack_storage";

int esp32_storage_init(void)
{
    ESP_LOGI(TAG, "Initializing SPIFFS...");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = FMRACK_SPIFFS_MOUNT,
        .partition_label = "storage",
        .max_files = 10,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount SPIFFS filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "SPIFFS partition not found");
        } else {
            ESP_LOGE(TAG, "SPIFFS init failed: %s", esp_err_to_name(ret));
        }
        return -1;
    }

    // Print filesystem info
    size_t total = 0, used = 0;
    ret = esp_spiffs_info("storage", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS mounted: total=%d bytes, used=%d bytes (%.1f%%)",
                 (int)total, (int)used,
                 total > 0 ? (float)used / total * 100.0f : 0.0f);
    }

    return 0;
}

void esp32_storage_deinit(void)
{
    esp_vfs_spiffs_unregister("storage");
    ESP_LOGI(TAG, "SPIFFS unmounted");
}

bool esp32_storage_file_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0);
}
