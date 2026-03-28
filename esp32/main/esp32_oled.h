#pragma once
/*
 * SH1106 OLED display - public API
 *
 * Shows the current chord name in large font on a 128×64 I2C display.
 * Pins: SDA=GPIO9, SCL=GPIO10  (see pins.md)
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the SH1106 OLED over I2C.
 * @return 0 on success, -1 on failure.
 */
int esp32_oled_init(void);

/**
 * Read step_seq_get_display_harmonic_state() and redraw the chord name.
 * Safe to call from any task; uses its own static u8g2 handle.
 * No-op if init failed.
 */
void esp32_oled_update_chord(void);

#ifdef __cplusplus
}
#endif
