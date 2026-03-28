#pragma once
/*
 * SH1106 OLED display — public API
 *
 * Default view  : chord name (big font) — root note + quality
 * Status overlay: any event triggers a 2-second overlay that auto-expires
 *
 * Pins: SDA=GPIO9, SCL=GPIO10  (see pins.md)
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize the SH1106 OLED over I2C. @return 0 ok, -1 fail. */
int  esp32_oled_init(void);

/** Redraw chord view (no-op while a status overlay is still visible). */
void esp32_oled_update_chord(void);

/** Show a two-line status overlay for 2 seconds, then return to chord view. */
void esp32_oled_show_status(const char *line1, const char *line2);

/* --- Typed convenience wrappers (called from state-change sites) --- */
void esp32_oled_show_mode(int mode);               /* seq_mode_t */
void esp32_oled_show_bpm(uint16_t bpm);
void esp32_oled_show_key(uint8_t key);             /* 0-11 */
void esp32_oled_show_scale(int scale);             /* scale_type_t */
void esp32_oled_show_tension(uint8_t tension);     /* 0-3 */
void esp32_oled_show_octave(uint8_t octave);
void esp32_oled_show_transport(bool playing);
void esp32_oled_show_fx(int fx_mode);              /* 0=both 1=rev 2=sym 3=off */
void esp32_oled_show_wlan(bool on);

#ifdef __cplusplus
}
#endif
