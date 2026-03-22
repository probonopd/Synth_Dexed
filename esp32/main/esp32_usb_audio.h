/*
 * FMRack ESP32-S3 Port – USB Audio Class (UAC 1.0) Output
 *
 * Registers a second USB host client (alongside the MIDI client) that
 * watches for USB audio output devices.  When one is found it:
 *
 *   1. Claims the AudioControl and AudioStreaming interfaces.
 *   2. Sends SET_INTERFACE to activate the streaming alternate setting.
 *   3. Streams synthesizer audio via isochronous OUT transfers.
 *
 * Channel mixing:
 *   The synth produces stereo int16.  The USB device may have 1-7 channels.
 *   - 1 channel  → mono: (L + R) / 2
 *   - 2 channels → stereo pass-through
 *   - 3-7 ch     → L on ch0, R on ch1, silence on remaining channels
 *
 * Sample rate:
 *   Exact match: audio written as-is.
 *   44100 Hz device with 48000 Hz Dexed (or vice-versa): linear interpolation.
 *   Other mismatches: device is skipped.
 *
 * Bit depth: 16-bit PCM only.  Devices reporting other depths are skipped.
 *
 * Audio routing:
 *   When a compatible USB audio device is ready the audio write task writes
 *   into a ring buffer; a dedicated USB audio task drains it via isochronous
 *   transfers.  The I2S path continues running in parallel so the on-board
 *   DAC also produces audio (useful as a monitor / fallback).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register the USB audio host client.  Call after usb_host_install() has
 * been called (i.e. after esp32_midi_init() which installs the USB host).
 * @return 0 on success, -1 on failure.
 */
int esp32_usb_audio_init(void);

/**
 * Deregister client and free resources.  Safe to call even if init failed.
 */
void esp32_usb_audio_stop(void);

/**
 * True when a compatible USB audio output device is streaming.
 */
bool esp32_usb_audio_is_ready(void);

/**
 * Number of output channels on the current USB audio device (0 if none).
 */
int esp32_usb_audio_get_channels(void);

/**
 * Push @p num_stereo_pairs stereo int16 sample pairs into the USB audio ring
 * buffer.  Non-blocking: if the ring is full the oldest samples are
 * overwritten (a brief dropout is preferable to blocking the audio thread).
 * Does nothing if no USB audio device is ready.
 *
 * @param buf             Interleaved stereo int16: [L0,R0, L1,R1, ...]
 * @param num_stereo_pairs Number of L/R pairs (i.e. buffer length / 2).
 */
void esp32_usb_audio_write_stereo(const int16_t *buf, int num_stereo_pairs);

#ifdef __cplusplus
}
#endif
