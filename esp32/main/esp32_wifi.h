/*
 * FMRack ESP32-S3 Port – WLAN management
 *
 * Manages the complete WLAN lifecycle:
 *   - Loads credentials from NVS and connects in Station mode.
 *   - On success starts Apple MIDI + mDNS so the synth appears in
 *     Audio MIDI Setup on the Mac automatically.
 *   - Falls back to a captive Access Point ("Synth-Dexed-Setup") when
 *     no credentials are stored or the network is unreachable, and runs
 *     a web-based credential form so the user can configure the network.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * Primary API
 * ------------------------------------------------------------------ */

/**
 * Initialise WLAN.
 * Loads stored credentials and tries STA mode, or starts the captive AP.
 * Also brings up Apple MIDI and mDNS when a STA connection is established.
 *
 * @return 0 always (errors are handled internally by falling back to AP).
 */
int esp32_wlan_init(void);

/**
 * Tear down WLAN, Apple MIDI, and the captive portal.
 */
void esp32_wlan_stop(void);

/**
 * @return true when the device has a STA connection to the user's network.
 */
bool esp32_wlan_is_connected(void);

/**
 * @return true when the device is running the captive-AP setup portal.
 */
bool esp32_wlan_is_ap_mode(void);

/* ------------------------------------------------------------------
 * Legacy shims – kept for source compatibility with main.cpp
 * ------------------------------------------------------------------ */
int  esp32_wifi_init(void);
int  esp32_wifi_udp_start(void);   /* no-op: superseded by Apple MIDI  */
void esp32_wifi_stop(void);
bool esp32_wifi_is_connected(void);

#ifdef __cplusplus
}
#endif
