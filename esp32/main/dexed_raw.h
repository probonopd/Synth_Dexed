/*
 * ESP32-S3 Port - Raw Dexed Engine Wrapper
 *
 * Provides a minimal, single-instance Dexed engine interface for the ESP32
 * platform code (audio + MIDI + optional SysEx).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int dexed_raw_init(int sample_rate);
void dexed_raw_deinit(void);

bool dexed_raw_is_initialized(void);
int dexed_raw_get_active_voices(void);

void dexed_raw_process_audio(float *left_out, float *right_out, int num_samples);

// Direct int16 mono output – skips the float round-trip.
// Writes `num_samples` mono 16-bit PCM samples into `out`.
void dexed_raw_process_audio_i16(int16_t *out, int num_samples);

void dexed_raw_handle_midi(uint8_t status, uint8_t data1, uint8_t data2);
void dexed_raw_handle_sysex(const uint8_t *data, int len, uint8_t channel);

// Optional: load 156-byte VoiceData1 from an FMRack-style performance.ini.
// Only the VoiceData1=... line is parsed; everything else is ignored.
// Returns 0 on success, non-zero on failure.
int dexed_raw_load_voice_from_performance_ini(const char *path);

// Convenience: read a .syx file from the filesystem and feed it to Dexed.
// Returns 0 on success.
int dexed_raw_send_sysex_file(const char *path, uint8_t channel);

// Load a single voice from a 32-voice DX7/TX7 bank dump (.syx, typically 4104 bytes).
// voice_index0: 0..31.
// Returns 0 on success.
int dexed_raw_load_voice_from_bank_syx(const char *path, int voice_index0);

// Load and cache all 32 voices from a bank dump (.syx, 4104 bytes).
// Returns 0 on success.
int dexed_raw_load_bank_syx(const char *path);

// Select a cached bank program (voice) by 0-based index (0..31).
// Returns 0 on success.
int dexed_raw_select_bank_program(int voice_index0);

// Returns true once after a voice change (poll from non-audio thread).
bool dexed_raw_poll_voice_changed(void);

// Copies the current voice name (trimmed, null-terminated) into out[len].
void dexed_raw_get_current_voice_name(char *out, int len);

#ifdef __cplusplus
}
#endif
