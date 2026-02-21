/*
 * FMRack ESP32-C5 Port - SPIFFS Storage
 *
 * Handles mounting the SPIFFS filesystem for loading performance
 * files and DX7 voice banks.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize and mount the SPIFFS filesystem.
 * @return 0 on success, -1 on failure.
 */
int esp32_storage_init(void);

/**
 * Unmount the SPIFFS filesystem.
 */
void esp32_storage_deinit(void);

/**
 * Check if SPIFFS is mounted and a file exists.
 * @param path Full path to the file.
 * @return true if the file exists.
 */
bool esp32_storage_file_exists(const char *path);

#ifdef __cplusplus
}
#endif
