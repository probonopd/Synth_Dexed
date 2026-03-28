/*
 * ESP32 Button Input Handler
 *
 * Handles GPIO interrupt for the BOOT button (GPIO 0, active-low).
 * Button press triggers symphonic effect toggle.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize button input handling.
 * Sets up GPIO 0 interrupt and creates button handler task.
 * @return 0 on success, -1 on failure.
 */
int esp32_button_init(void);

/**
 * Deinitialize button handler.
 */
void esp32_button_deinit(void);

/**
 * Returns true when the user has long-pressed to enable WLAN mode.
 * Stays true until another long-press disables WLAN.
 * Use this to drive the LED indicator during wlan_init().
 */
bool esp32_button_is_wlan_mode(void);

#ifdef __cplusplus
}
#endif
