/*
 * FMRack ESP32-S3 Port – Captive WLAN Configuration Portal
 *
 * When no WLAN credentials are stored (or the stored credentials fail),
 * the device starts its own Access Point ("Synth-Dexed-Setup") and runs
 * this captive portal so the user can enter their home network credentials
 * from a phone or laptop without flashing new firmware.
 *
 * What runs:
 *   – UDP DNS server on port 53 (redirects all queries to 192.168.4.1)
 *   – HTTP server on port 80 (credentials form + submission handler)
 *
 * Flow:
 *   1. User connects phone/laptop to the "Synth-Dexed-Setup" WLAN network.
 *   2. OS detects a captive portal and opens a browser automatically.
 *   3. User fills in SSID and password and taps "Connect".
 *   4. Credentials are stored in NVS and the device reboots into STA mode.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the captive portal (DNS redirector + HTTP credential form).
 * Must be called while the Wi-Fi driver is running in AP mode.
 *
 * @return 0 on success, -1 on failure.
 */
int esp32_captive_portal_start(void);

/**
 * Stop the captive portal and free all resources.
 */
void esp32_captive_portal_stop(void);

/**
 * Load WLAN credentials from NVS.
 *
 * @param ssid  Buffer for SSID (at least 33 bytes).
 * @param pass  Buffer for password (at least 65 bytes).
 * @return true if credentials were found and loaded.
 */
bool esp32_wlan_load_credentials(char *ssid, char *pass);

/**
 * Save WLAN credentials to NVS.
 *
 * @param ssid  SSID string (max 32 chars).
 * @param pass  Password string (max 64 chars, may be empty).
 * @return 0 on success, -1 on failure.
 */
int esp32_wlan_save_credentials(const char *ssid, const char *pass);

/**
 * Erase stored WLAN credentials from NVS.
 */
void esp32_wlan_erase_credentials(void);

#ifdef __cplusplus
}
#endif
