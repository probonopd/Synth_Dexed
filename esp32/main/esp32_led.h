/*
 * FMRack ESP32-S3 Port - RGB LED Status Indicator
 *
 * Drives the WS2812 addressable RGB LED on GPIO48 (ESP32-S3-DevKitC-1)
 * to communicate system state visually.
 *
 * Color scheme:
 *   RED        = Booting / initializing
 *   YELLOW     = Engine starting (audio/MIDI init)
 *   BLUE       = Ready, waiting for MIDI input
 *   GREEN      = USB MIDI keyboard connected
 *   CYAN pulse = Notes playing (active voices)
 *   WHITE      = Startup sound playing
 *   OFF        = Error / not initialized
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LED states used by the main application */
typedef enum {
    LED_STATE_OFF = 0,       /* LED off */
    LED_STATE_BOOTING,       /* Red — early boot */
    LED_STATE_ENGINE_INIT,   /* Yellow — engine/audio init */
    LED_STATE_STARTUP_SOUND, /* White — playing startup chord */
    LED_STATE_READY,         /* Blue — ready, no USB keyboard */
    LED_STATE_USB_CONNECTED, /* Green — USB MIDI keyboard attached */
    LED_STATE_PLAYING,       /* Cyan pulse — notes are sounding */
    LED_STATE_WLAN_ACTIVE,   /* Orange pulse — WLAN mode on, no notes */
    LED_STATE_ERROR,         /* Red fast blink — error */
} led_state_t;

/**
 * Initialize the WS2812 LED on GPIO48 using RMT.
 * Call once during early boot.
 * @return 0 on success, -1 on failure.
 */
int esp32_led_init(void);

/**
 * Set the LED to a specific color (GRB order, WS2812).
 * @param r Red   (0-255)
 * @param g Green (0-255)
 * @param b Blue  (0-255)
 */
void esp32_led_set_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * Set the current LED state. The LED task will animate accordingly.
 */
void esp32_led_set_state(led_state_t state);

/**
 * Get the current LED state.
 */
led_state_t esp32_led_get_state(void);

/**
 * Set the current effects mode (0=both, 1=reverb only, 2=symphonic only, 3=neither).
 * ISR-safe — just writes a volatile.  The LED task uses this to select the
 * indication color for READY / USB_CONNECTED / PLAYING states.
 *   Mode 0 (both on)       → cyan
 *   Mode 1 (reverb only)   → purple
 *   Mode 2 (symphonic only)→ amber
 *   Mode 3 (neither)       → cool white
 */
void esp32_led_set_fx_mode(int mode);

/**
 * Start the LED animation task (call after esp32_led_init).
 */
void esp32_led_start(void);

#ifdef __cplusplus
}
#endif
