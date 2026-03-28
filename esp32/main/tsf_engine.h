/*
 * TSF (TinySoundFont) Drum Engine
 *
 * Independent SoundFont synthesizer engine for drum sounds.
 * Loads OGG-compressed SoundFont files (.sfo) from SPIFFS.
 * Designed to work alongside the Dexed FM engine.
 *
 * Architecture:
 *   - SFO file decoded at init time; samples stored as floats in PSRAM.
 *   - MIDI queue (lock-free ring, same pattern as dexed_raw) accepts
 *     Note On/Off from any core.
 *   - Render function called from audio_render_task on Core 1.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =====================================================================
 * Error detail flags (returned by tsf_engine_get_error_detail())
 *
 * These are bitmask values; multiple flags may be set simultaneously.
 * Display on the Launchpad right-column LEDs via launchpad_show_tsf_error().
 *
 * LED mapping (CC 19=bit0/bottom ... CC 89=bit7/top):
 *   Bit  LED CC  Meaning
 *   0    19      File not found in SPIFFS
 *   1    29      File size invalid (empty, too large >4MB, or negative)
 *   2    39      PSRAM allocation failed for raw SFO read buffer
 *   3    49      Short read — fewer bytes than expected from SPIFFS
 *   4    59      stb_vorbis decode workspace (alloc_buf) failed to allocate
 *   5    69      stb_vorbis OOM — decode workspace too small (bump-allocator overflowed)
 *   6    79      stb_vorbis decode error — corrupt or unsupported OGG stream
 *   7    89      PSRAM exhausted during TSF sample decode (TSF_MALLOC failure)
 * ===================================================================== */
#define TSF_ERR_FILE_NOT_FOUND      (1u << 0)  /* 0x01 — file missing on SPIFFS */
#define TSF_ERR_FILE_SIZE           (1u << 1)  /* 0x02 — invalid file size */
#define TSF_ERR_PSRAM_ALLOC         (1u << 2)  /* 0x04 — no PSRAM for read buffer */
#define TSF_ERR_SHORT_READ          (1u << 3)  /* 0x08 — partial file read */
#define TSF_ERR_VORBIS_BUF          (1u << 4)  /* 0x10 — vorbis workspace alloc failed */
#define TSF_ERR_VORBIS_OOM          (1u << 5)  /* 0x20 — vorbis bump-allocator overflowed */
#define TSF_ERR_VORBIS_DECODE       (1u << 6)  /* 0x40 — bad/corrupt OGG stream */
#define TSF_ERR_TSF_MALLOC          (1u << 7)  /* 0x80 — PSRAM exhausted during decode */

/**
 * Initialize the TSF drum engine.
 * Loads the SFO file from SPIFFS into PSRAM.
 * @param sample_rate  Audio sample rate (must match the main audio pipeline).
 * @return 0 on success, -1 on failure (non-fatal: drums fall back to Dexed).
 */
int tsf_engine_init(int sample_rate);

/**
 * Deinitialize and free all TSF resources (PSRAM sample data, etc.).
 */
void tsf_engine_deinit(void);

/**
 * @return true if the TSF engine is loaded and ready to render.
 *         Safe to call from any core.
 */
bool tsf_engine_is_loaded(void);

/**
 * Handle a MIDI message for the drum engine.
 * Enqueues the message for processing in the next render call.
 * Thread-safe: uses a lock-free ring buffer with spinlock.
 *
 * @param status  MIDI status byte (e.g. 0x99 for Note On ch10, 0x89 for Note Off ch10)
 * @param data1   MIDI data byte 1 (note number)
 * @param data2   MIDI data byte 2 (velocity for Note On)
 */
void tsf_engine_handle_midi(uint8_t status, uint8_t data1, uint8_t data2);

/**
 * Render audio into a stereo interleaved int16 buffer.
 * Called from the audio render task on Core 1.
 * Drains the MIDI queue, then renders sample frames.
 *
 * @param out          Stereo interleaved int16 buffer (num_samples * 2 elements).
 * @param num_samples  Number of sample frames to render.
 */
void tsf_engine_render_stereo_i16(int16_t *out, int num_samples);

/**
 * Reload the SFO file from SPIFFS.
 * Called after a new SFO is uploaded via the web interface.
 * Temporarily pauses TSF rendering, reloads, then resumes.
 * @return 0 on success, -1 on failure.
 */
int tsf_engine_reload(void);

/**
 * @return number of currently active voices in the TSF engine.
 */
int tsf_engine_active_voices(void);

/** @return total number of presets in the loaded SFO. */
int tsf_engine_get_preset_count(void);

/** @return index of the currently active drum preset (0-based). */
int tsf_engine_get_current_preset(void);

/** Copy preset name at idx into out[len] (null-terminated). */
void tsf_engine_copy_preset_name(int idx, char *out, int len);

/** Select drum preset by index. Thread-safe. Returns 0 on success. */
int tsf_engine_select_preset(int idx);

/**
 * Load progress indicator (for boot-time diagnostics / Launchpad display).
 *
 *  0  = not started
 *  1  = file opened
 *  2  = file size validated
 *  3  = PSRAM buffer allocated
 *  4  = file read into buffer
 *  5  = tsf_load_memory() starting (OGG decode — the slow step)
 *  6  = tsf_load_memory() succeeded
 *  7  = output / channel configured
 *  8  = fully loaded (matches tsf_engine_is_loaded())
 * -1  = failed at step 1 (file not found)
 * -2  = failed at step 2 (file size invalid)
 * -3  = failed at step 3 (PSRAM alloc for buffer)
 * -4  = failed at step 4 (short read)
 * -5  = failed at step 5 (tsf_load_memory returned NULL)
 */
int tsf_engine_get_load_progress(void);

/**
 * Error detail bitmask (valid only when tsf_engine_get_load_progress() < 0).
 * Returns a combination of TSF_ERR_* flags describing every failure mode
 * encountered during the last load attempt.
 *
 * Use launchpad_show_tsf_error(tsf_engine_get_error_detail()) to display
 * the error code as binary on the right-column LEDs.
 *
 * @return uint8_t bitmask (TSF_ERR_* flags OR'd together), 0 if no errors.
 */
uint8_t tsf_engine_get_error_detail(void);

#ifdef __cplusplus
}
#endif
