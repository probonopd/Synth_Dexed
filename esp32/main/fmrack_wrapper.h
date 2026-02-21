/*
 * FMRack ESP32-S3 Port - Engine Wrapper
 *
 * Provides a C-compatible interface between the ESP32-S3 platform code
 * and the C++ FMRack engine (Rack, Module, Performance, etc.).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the FMRack synthesis engine.
 * Creates the Rack object and sets up the default performance.
 * @param sample_rate Audio sample rate in Hz.
 * @param num_modules Number of synth modules (1-8).
 * @return 0 on success, -1 on failure.
 */
int fmrack_init(int sample_rate, int num_modules);

/**
 * Deinitialize the FMRack engine and free all resources.
 */
void fmrack_deinit(void);

/**
 * Load a performance file from the filesystem.
 * @param path Full path to the performance .ini file.
 * @return 0 on success, -1 on failure.
 */
int fmrack_load_performance(const char *path);

/**
 * Process audio - render the next block of stereo samples.
 * Called from the audio task.
 * @param left_out  Left channel output buffer (float, -1.0 to 1.0).
 * @param right_out Right channel output buffer (float, -1.0 to 1.0).
 * @param num_samples Number of samples to render.
 */
void fmrack_process_audio(float *left_out, float *right_out, int num_samples);

/**
 * Handle an incoming MIDI message (3-byte channel messages).
 * Thread-safe - can be called from any task.
 * @param status MIDI status byte (0x80-0xEF).
 * @param data1  First data byte.
 * @param data2  Second data byte.
 */
void fmrack_handle_midi(uint8_t status, uint8_t data1, uint8_t data2);

/**
 * Handle an incoming SysEx message.
 * @param data    SysEx data (including F0 and F7).
 * @param len     Length of the SysEx data.
 * @param channel SysEx channel (0 for broadcast).
 */
void fmrack_handle_sysex(const uint8_t *data, int len, uint8_t channel);

/**
 * Handle a program change (loads performance file by number).
 * @param program Program number (0-based).
 * @param dir     Directory to search for performance files.
 */
void fmrack_handle_program_change(int program, const char *dir);

/**
 * Get the number of active voices across all modules.
 * @return Total number of active voices.
 */
int fmrack_get_active_voices(void);

/**
 * Get the number of enabled parts.
 * @return Number of enabled parts.
 */
int fmrack_get_enabled_parts(void);

/**
 * Check if the engine is initialized and ready.
 * @return true if initialized.
 */
bool fmrack_is_initialized(void);

/**
 * Run a standalone Dexed engine test (bypasses Rack/Module).
 * Logs whether the raw Dexed engine produces audio on this platform.
 */
void fmrack_test_dexed_standalone(void);

#ifdef __cplusplus
}
#endif
