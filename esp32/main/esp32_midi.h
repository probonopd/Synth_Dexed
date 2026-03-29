/*
 * FMRack ESP32-S3 Port - MIDI Input
 *
 * Handles MIDI input from three sources:
 * 1. Hardware UART (DIN-5 MIDI at 31250 baud)
 * 2. USB-MIDI class device via USB OTG (class-compliant, no host drivers needed)
 * 3. UDP MIDI over Wi-Fi (handled separately in esp32_wifi.cpp)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize MIDI input (UART + USB-MIDI device).
 * @return 0 on success, -1 on failure.
 */
int esp32_midi_init(void);

/**
 * Start the MIDI processing task(s).
 * @return 0 on success, -1 on failure.
 */
int esp32_midi_start(void);

/**
 * Stop MIDI processing.
 */
void esp32_midi_stop(void);

/**
 * Check if USB-MIDI is currently connected to a host.
 * @return true if a USB host has mounted the MIDI interface.
 */
bool esp32_midi_usb_connected(void);

/**
 * Check if the connected USB MIDI device is a Novation Launchpad.
 * @return true if a Launchpad is connected.
 */
bool esp32_midi_is_launchpad(void);

/**
 * Send raw USB-MIDI 4-byte packets to the connected device.
 * @param packets  Buffer of 4-byte USB-MIDI event packets.
 * @param len      Total byte count (must be a multiple of 4).
 * @return 0 on success, -1 on error or no device.
 */
int esp32_midi_usb_send_packets(const uint8_t *packets, int len);

/**
 * Send a complete SysEx message via USB-MIDI OUT.
 * The message must include F0 at start and F7 at end.
 * @param sysex  Complete SysEx message (F0 ... F7).
 * @param len    Byte length of the message.
 * @return 0 on success, -1 on error or no device.
 */
int esp32_midi_usb_send_sysex(const uint8_t *sysex, int len);

/**
 * Send a single 3-byte MIDI channel message via USB-MIDI OUT.
 * @return 0 on success, -1 on error.
 */
int esp32_midi_usb_send_msg(uint8_t status, uint8_t data1, uint8_t data2);

/**
 * Get array of currently-held external MIDI notes for chord guessing.
 * @param out_notes  Pointer to receive 128-bool array of active notes.
 */
void esp32_midi_get_external_notes(bool out_notes[128]);

#ifdef __cplusplus
}
#endif
