/*
 * FMRack ESP32-C5 Port - MIDI Input
 *
 * Handles MIDI input from:
 * 1. Hardware UART (DIN-5 MIDI at 31250 baud)
 * 2. USB Serial (CDC) MIDI
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize MIDI input (UART + USB).
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

#ifdef __cplusplus
}
#endif
