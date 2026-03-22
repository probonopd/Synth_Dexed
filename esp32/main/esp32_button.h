/*
 * ESP32 Button Input Handler
 *
 * Handles GPIO interrupt for the BOOT button (GPIO 0, active-low).
 * Button press triggers symphonic effect toggle.
 */

#pragma once

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

#ifdef __cplusplus
}
#endif
