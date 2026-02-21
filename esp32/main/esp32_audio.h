/*
 * FMRack ESP32-S3 Port - Audio Output (I2S)
 *
 * Handles I2S initialization and the audio processing task that
 * feeds synthesized audio to the DAC.
 * On ESP32-S3, the audio task is pinned to core 1 for dedicated
 * real-time processing, and MCLK output is enabled for DACs that
 * require it.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the I2S audio output peripheral.
 * Configures I2S in Philips standard mode with MCLK on the ESP32-S3.
 * Must be called before starting the audio task.
 * @return 0 on success, -1 on failure.
 */
int esp32_audio_init(void);

/**
 * Start the audio processing task (pinned to core 1).
 * This creates a high-priority FreeRTOS task that continuously
 * generates audio samples from the FMRack engine and sends them
 * to the I2S DAC.
 * @return 0 on success, -1 on failure.
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

/**
 * Start a short test sine wave (generated in the audio task).
 * Useful to validate I2S wiring / amplifier independently of the synth engine.
 *
 * @param freq_hz Frequency in Hz (e.g. 440.0f)
 * @param seconds Duration in seconds (e.g. 1.0f)
 * @param amplitude Linear amplitude [0..1], keep <= 0.3 for comfort.
 */
void esp32_audio_start_test_tone(float freq_hz, float seconds, float amplitude);

#ifdef __cplusplus
}
#endif
