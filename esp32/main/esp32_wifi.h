/*
 * FMRack ESP32-C5 Port - Wi-Fi and UDP MIDI
 *
 * Handles Wi-Fi connectivity and UDP MIDI input over the network.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize Wi-Fi (STA or AP mode depending on configuration).
 * @return 0 on success, -1 on failure.
 */
int esp32_wifi_init(void);

/**
 * Start the UDP MIDI server task.
 * @return 0 on success, -1 on failure.
 */
int esp32_wifi_udp_start(void);

/**
 * Stop Wi-Fi and UDP server.
 */
void esp32_wifi_stop(void);

/**
 * Check if Wi-Fi is connected.
 * @return true if connected.
 */
bool esp32_wifi_is_connected(void);

#ifdef __cplusplus
}
#endif
