/*
 * esp32_oled.cpp — SH1106 128×64 OLED chord-name display
 *
 * Uses the nixy4/u8g2 managed component (ESP-IDF fork of olikraus/u8g2).
 * Driver:  u8g2_Setup_sh1106_i2c_128x64_noname_f (full-buffer, 1 kB RAM)
 * Fonts:   u8g2_font_logisoso38_tr  — root note  (e.g. "C#")
 *          u8g2_font_logisoso20_tr  — chord type (e.g. "maj", "m7")
 * Layout (128×64 px):
 *   y  2–40  : root note  (logisoso38, baseline y=40)
 *   y 42–62  : chord type (logisoso20, baseline y=62)
 *
 * I2C pins: SDA=GPIO9, SCL=GPIO10  (see pins.md)
 * I2C addr: 0x3C (7-bit)
 */

#include "esp32_oled.h"
#include "step_sequencer.h"

#include "esp_log.h"
#include <string.h>

#include "u8g2.h"
#include "esp32_hw_i2c.h"

static const char *TAG = "oled";

#define OLED_I2C_ADDR    0x3C   /* 7-bit */
#define OLED_SDA_PIN     9
#define OLED_SCL_PIN     10

static u8g2_t               s_u8g2;
static u8g2_esp32_i2c_ctx_t s_i2c_ctx;
static bool                 s_initialized = false;

/* Mirror of the chord name tables in step_sequencer.cpp */
static const char * const s_note_names[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
static const char * const s_chord_type_names[CHORD_TYPE_COUNT] = {
    "maj", "min", "7", "m7", "dim", "aug", "sus4", "sus2"
};

/* ------------------------------------------------------------------ */

int esp32_oled_init(void)
{
    /* Configure the ESP32 I2C hardware context used by u8g2 */
    s_i2c_ctx.cfg.i2c_port      = 0;
    s_i2c_ctx.cfg.sda_pin       = OLED_SDA_PIN;
    s_i2c_ctx.cfg.scl_pin       = OLED_SCL_PIN;
    s_i2c_ctx.cfg.clk_hz        = 400000;
    s_i2c_ctx.cfg.dev_addr_7bit = OLED_I2C_ADDR;
    s_i2c_ctx.cfg.timeout_ms    = 1000;
    s_i2c_ctx.cfg.reset_pin     = U8G2_ESP32_PIN_UNUSED;

    esp_err_t err = u8g2_esp32_i2c_set_default_context(&s_i2c_ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C context init failed: %s", esp_err_to_name(err));
        return -1;
    }

    /* SH1106 128×64 I2C, full-buffer mode */
    u8g2_Setup_sh1106_i2c_128x64_noname_f(
        &s_u8g2,
        U8G2_R0,
        u8x8_byte_esp32_hw_i2c,
        u8x8_gpio_and_delay_esp32_i2c
    );

    /* u8g2 needs the 8-bit I2C address (7-bit << 1) */
    u8x8_SetI2CAddress(&s_u8g2.u8x8, OLED_I2C_ADDR << 1);

    u8g2_InitDisplay(&s_u8g2);
    u8g2_SetPowerSave(&s_u8g2, 0);   /* display on */

    s_initialized = true;
    ESP_LOGI(TAG, "SH1106 128x64 OLED ready (SDA=%d SCL=%d addr=0x%02X)",
             OLED_SDA_PIN, OLED_SCL_PIN, OLED_I2C_ADDR);

    /* Splash screen */
    u8g2_ClearBuffer(&s_u8g2);
    u8g2_SetFont(&s_u8g2, u8g2_font_logisoso24_tr);
    int sw = (int)u8g2_GetStrWidth(&s_u8g2, "Synth Dexed");
    u8g2_DrawStr(&s_u8g2, (128 - sw) / 2, 40, "Synth Dexed");
    u8g2_SendBuffer(&s_u8g2);

    return 0;
}

void esp32_oled_update_chord(void)
{
    if (!s_initialized) return;

    const harmonic_state_t *h = step_seq_get_display_harmonic_state();
    if (!h) return;

    /* Absolute chord root: key + chord_root (both 0-11) */
    uint8_t abs_root = (uint8_t)((h->key + h->chord_root) % 12);
    const char *note  = s_note_names[abs_root];
    const char *ctype = s_chord_type_names[(int)h->chord_type];

    u8g2_ClearBuffer(&s_u8g2);

    /* ── Line 1: root note in large font, horizontally centered ── */
    u8g2_SetFont(&s_u8g2, u8g2_font_logisoso38_tr);
    int w1 = (int)u8g2_GetStrWidth(&s_u8g2, note);
    int x1 = (128 - w1) / 2;
    if (x1 < 0) x1 = 0;
    u8g2_DrawStr(&s_u8g2, (u8g2_uint_t)x1, 40, note);  /* baseline y=40 */

    /* ── Line 2: chord type in medium font, horizontally centered ── */
    u8g2_SetFont(&s_u8g2, u8g2_font_logisoso20_tr);
    int w2 = (int)u8g2_GetStrWidth(&s_u8g2, ctype);
    int x2 = (128 - w2) / 2;
    if (x2 < 0) x2 = 0;
    u8g2_DrawStr(&s_u8g2, (u8g2_uint_t)x2, 62, ctype); /* baseline y=62 */

    u8g2_SendBuffer(&s_u8g2);
}
