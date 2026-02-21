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

#ifdef __cplusplus
}
#endif
