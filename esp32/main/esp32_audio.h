/*
 * FMRack ESP32-C5 Port - Audio Output (I2S)
 *
 * Handles I2S initialization and the audio processing task that
 * feeds synthesized audio to the DAC.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the I2S audio output peripheral.
 * Must be called before starting the audio task.
 * @return ESP_OK on success, error code on failure.
 */
int esp32_audio_init(void);

/**
 * Start the audio processing task.
 * This creates a high-priority FreeRTOS task that continuously
 * generates audio samples from the FMRack engine and sends them
 * to the I2S DAC.
 * @return ESP_OK on success, error code on failure.
 */
int esp32_audio_start(void);

/**
 * Stop the audio processing task and deinitialize I2S.
 */
void esp32_audio_stop(void);

/**
 * Check if audio is currently running.
 * @return true if the audio task is active.
 */
bool esp32_audio_is_running(void);

#ifdef __cplusplus
}
#endif
