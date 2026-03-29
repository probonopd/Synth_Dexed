/*
 * esp32_oled.cpp — SH1106 128×64 OLED display driver
 *
 * Two display modes:
 *   1. Chord view (default): root note in big font + chord type
 *   2. Status overlay (2 s timeout): label + value for mode/BPM/FX/etc.
 *
 * Uses nixy4/u8g2 component — full upstream u8g2 API, real bitmap fonts.
 * Driver:  u8g2_Setup_sh1106_i2c_128x64_noname_f
 * I2C:     SDA=GPIO9, SCL=GPIO10, addr=0x3C  (see pins.md)
 *
 * Public API:
 *   esp32_oled_init()              — call once at boot
 *   esp32_oled_update_chord()      — call from launchpad_refresh_grid()
 *   esp32_oled_show_status(l1,l2)  — call from any state-change site;
 *                                    auto-returns to chord view after 2 s
 */

#include "esp32_oled.h"
#include "step_sequencer.h"
#include "dexed_raw.h"
#include "chord_guesser.h"
#include "esp32_midi.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

#include "u8g2.h"
#include "esp32_hw_i2c.h"

static const char *TAG = "oled";

/* ---- Hardware ---- */
#define OLED_I2C_ADDR       0x3C
#define OLED_SDA_PIN        9
#define OLED_SCL_PIN        10

/* ---- Status overlay timeout ---- */
#define STATUS_DURATION_US  2000000LL   /* 2 seconds */

static u8g2_t               s_u8g2;
static u8g2_esp32_i2c_ctx_t s_i2c_ctx;
static bool                 s_initialized = false;

/* Timestamp after which status view expires; 0 = chord view */
static volatile int64_t     s_status_until_us = 0;

/* Alternative chord suggestions for display */
static chord_guess_t        s_alternatives[3] = {};
static int                  s_alternatives_count = 0;

/* ---- String tables ---- */
static const char * const s_note_names[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
static const char * const s_chord_type_names[] = {
    "maj", "min", "7", "m7", "dim", "aug", "sus4", "sus2"
};
/* Must match seq_mode_t: DRUM=0 MELODIC=1 BOTH=2 CIRCLE=3 FIELD=4 */
static const char * const s_mode_names[] = {
    "DRUM", "MELODIC", "BOTH", "CIRCLE", "FIELD"
};
static const char * const s_scale_names[] = {
    "Major", "Minor", "Dorian", "Mixolyd",
    "Phryg", "Lydian", "Locrian", "Penta+", "Penta-"
};
static const char * const s_fx_names[] = {
    "Rev+Sym", "Reverb", "Symphon", "FX Off"
};

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static void draw_centered(u8g2_t *u, u8g2_uint_t y, const char *str)
{
    u8g2_uint_t w = u8g2_GetStrWidth(u, str);
    int px = (128 - (int)w) / 2;
    u8g2_uint_t x = (px < 0) ? 0 : (u8g2_uint_t)px;
    u8g2_DrawStr(u, x, y, str);
}

/* ---- Chord view ---- */
static void draw_chord(void)
{
    u8g2_ClearBuffer(&s_u8g2);

    /* Check if there's a guessed chord from keyboard input (only when stopped) */
    uint8_t guess_root = 0, guess_type = 0, guess_score = 0;
    bool has_guess = (step_seq_get_guessed_chord(&guess_root, &guess_type, &guess_score) == 0);

    if (has_guess && !step_seq_is_playing()) {
        /* Check if this is a single note (type == 0xFF) or a chord */
        if (guess_type == 0xFF) {
            /* Single note display */
            const char *note = s_note_names[guess_root];
            
            u8g2_SetFont(&s_u8g2, u8g2_font_logisoso38_tr);
            draw_centered(&s_u8g2, 42, note);
            
            /* Add "(note)" label at bottom */
            u8g2_SetFont(&s_u8g2, u8g2_font_6x13_tr);
            draw_centered(&s_u8g2, 62, "note");
        } else {
            /* Display guessed chord from keyboard */
            const char *note = s_note_names[guess_root];
            const char *ctype = s_chord_type_names[guess_type];

            char fullname[14] = {0};
            snprintf(fullname, sizeof(fullname), "%s%s", note, ctype);

            /* Guessed chord — centered, large */
            u8g2_SetFont(&s_u8g2, u8g2_font_logisoso38_tr);
            draw_centered(&s_u8g2, 38, fullname);

            /* Show alternatives in small font below */
            u8g2_SetFont(&s_u8g2, u8g2_font_5x7_tf);
            for (int i = 1; i < s_alternatives_count && i < 3; i++) {
                const char *alt_note = s_note_names[s_alternatives[i].root];
                const char *alt_type = s_chord_type_names[s_alternatives[i].type];
                char alt_name[10] = {0};
                snprintf(alt_name, sizeof(alt_name), "%s%s", alt_note, alt_type);
                draw_centered(&s_u8g2, 50 + (i-1)*10, alt_name);
            }
        }
    } else {
        /* Display programmed chord (normal mode) */
        const harmonic_state_t *h = step_seq_get_display_harmonic_state();
        if (!h) return;

        uint8_t abs_root = (uint8_t)((h->key + h->chord_root) % 12);
        const char *note  = s_note_names[abs_root];
        const char *ctype = s_chord_type_names[(int)h->chord_type];

        /* Roman numeral for the current scale degree */
        char roman[8] = {0};
        step_seq_chord_roman_numeral(h->chord_root, h->chord_type,
                                     h->scale_type, roman, sizeof(roman));

        /* Full chord name e.g. "Gmaj", "Am7" */
        char fullname[14] = {0};
        snprintf(fullname, sizeof(fullname), "%s%s", note, ctype);

        /* Roman numeral — centered, large */
        u8g2_SetFont(&s_u8g2, u8g2_font_logisoso38_tr);
        draw_centered(&s_u8g2, 42, roman);

        /* Full chord name — centered, medium */
        u8g2_SetFont(&s_u8g2, u8g2_font_logisoso20_tr);
        draw_centered(&s_u8g2, 62, fullname);
    }

    u8g2_SendBuffer(&s_u8g2);
}

/* ---- Status overlay ---- */
static void draw_status(const char *label, const char *value)
{
    u8g2_ClearBuffer(&s_u8g2);

    /* Small label at top */
    u8g2_SetFont(&s_u8g2, u8g2_font_6x13_tr);
    draw_centered(&s_u8g2, 14, label);

    /* Thin separator line */
    u8g2_DrawHLine(&s_u8g2, 0, 17, 128);

    /* Big value */
    u8g2_SetFont(&s_u8g2, u8g2_font_logisoso24_tr);
    draw_centered(&s_u8g2, 56, value);

    u8g2_SendBuffer(&s_u8g2);
}

static void oled_set_alternatives(const chord_guess_t* alts, int count)
{
    if (count > 3) count = 3;
    s_alternatives_count = count;
    for (int i = 0; i < count; i++) {
        s_alternatives[i] = alts[i];
    }
}

/* ------------------------------------------------------------------ */
/*  Background voice-change poller                                       */
/* ------------------------------------------------------------------ */

static void oled_poll_task(void *)
{
    static uint64_t last_guess_update = 0;
    
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (!s_initialized) continue;

        /* Voice change: show immediately on OLED */
        if (dexed_raw_poll_voice_changed()) {
            char vname[12] = {0};
            dexed_raw_get_current_voice_name(vname, sizeof(vname));
            if (vname[0] != '\0') {
                esp32_oled_show_status("VOICE", vname);
            }
        }

        /* Outro fill done: stop the sequencer from the main task context */
        if (step_seq_consume_outro_fill_done()) {
            step_seq_stop();
        }
        
        /* Chord guesser: run periodically when sequencer is stopped */
        uint64_t now = esp_timer_get_time();
        if (!step_seq_is_playing() && (now - last_guess_update) >= 100000) {
            last_guess_update = now;
            
            /* Get all active notes (field pads + external keyboard) */
            bool active_notes[128] = {false};
            step_seq_get_active_notes(active_notes);
            
            /* Count active pitch classes */
            uint16_t pitch_classes = 0;
            int active_pitch_count = 0;
            int single_note_pc = -1;
            
            for (int note = 0; note < 128; note++) {
                if (active_notes[note]) {
                    int pc = note % 12;
                    if (!((pitch_classes >> pc) & 1)) {
                        active_pitch_count++;
                        single_note_pc = pc;
                    }
                    pitch_classes |= (1 << pc);
                }
            }
            
            if (active_pitch_count == 1 && single_note_pc >= 0) {
                /* Single note: display just the note name */
                step_seq_update_guessed_chord(single_note_pc, 0xFF, 255);  /* type 0xFF = single note marker */
                ESP_LOGI(TAG, "Single note: %d", single_note_pc);
                esp32_oled_update_chord();
            } else if (active_pitch_count >= 3) {
                /* Multiple notes: analyze for chords with harmonic context */
                const harmonic_state_t* harmonic = step_seq_get_harmonic_state();
                chord_guess_t guesses[3];
                int guess_count = chord_guesser_analyze(active_notes, harmonic, guesses, 3);
                
                if (guess_count > 0) {
                    /* Good match found, update state with top guess and trigger display */
                    step_seq_update_guessed_chord(guesses[0].root, guesses[0].type, guesses[0].score);
                    
                    /* Store alternatives for display */
                    oled_set_alternatives(guesses, guess_count);
                    
                    ESP_LOGI(TAG, "Guessed chord: root=%u type=%u score=%u", guesses[0].root, guesses[0].type, guesses[0].score);
                    esp32_oled_update_chord();
                } else {
                    /* No valid chord */
                    step_seq_update_guessed_chord(0xFF, 0xFF, 0);
                }
            } else {
                /* 0 or 2 notes: don't display */
                step_seq_update_guessed_chord(0xFF, 0xFF, 0);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                           */
/* ------------------------------------------------------------------ */

int esp32_oled_init(void)
{
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

    u8g2_Setup_sh1106_i2c_128x64_noname_f(
        &s_u8g2,
        U8G2_R0,
        u8x8_byte_esp32_hw_i2c,
        u8x8_gpio_and_delay_esp32_i2c
    );
    u8x8_SetI2CAddress(&s_u8g2.u8x8, OLED_I2C_ADDR << 1);
    u8g2_InitDisplay(&s_u8g2);
    u8g2_SetPowerSave(&s_u8g2, 0);

    s_initialized = true;
    ESP_LOGI(TAG, "SH1106 128x64 ready (SDA=%d SCL=%d)", OLED_SDA_PIN, OLED_SCL_PIN);

    /* Splash screen */
    u8g2_ClearBuffer(&s_u8g2);
    u8g2_SetFont(&s_u8g2, u8g2_font_logisoso24_tr);
    draw_centered(&s_u8g2, 42, "...");
    u8g2_SendBuffer(&s_u8g2);

    /* Polling task: detects voice changes immediately */
    xTaskCreate(oled_poll_task, "oled_poll", 4096, nullptr, 2, nullptr);

    return 0;
}

void esp32_oled_update_chord(void)
{
    if (!s_initialized) return;
    if (esp_timer_get_time() < s_status_until_us) return;  /* status still active */
    draw_chord();
}

void esp32_oled_show_status(const char *line1, const char *line2)
{
    if (!s_initialized) return;
    s_status_until_us = esp_timer_get_time() + STATUS_DURATION_US;
    draw_status(line1 ? line1 : "", line2 ? line2 : "");
}

/* ---- Convenience wrappers ---- */

void esp32_oled_show_mode(int mode)
{
    if (!s_initialized) return;
    const char *name = (mode >= 0 && mode < 5) ? s_mode_names[mode] : "?";
    esp32_oled_show_status("MODE", name);
}

void esp32_oled_show_bpm(uint16_t bpm)
{
    if (!s_initialized) return;
    char val[8];
    snprintf(val, sizeof(val), "%u", (unsigned)bpm);
    esp32_oled_show_status("BPM", val);
}

void esp32_oled_show_key(uint8_t key)
{
    if (!s_initialized) return;
    const char *name = (key < 12) ? s_note_names[key] : "?";
    esp32_oled_show_status("KEY", name);
}

void esp32_oled_show_scale(int scale)
{
    if (!s_initialized) return;
    const char *name = (scale >= 0 && scale < 9) ? s_scale_names[scale] : "?";
    esp32_oled_show_status("SCALE", name);
}

void esp32_oled_show_tension(uint8_t tension)
{
    if (!s_initialized) return;
    char val[4];
    snprintf(val, sizeof(val), "%u", (unsigned)tension);
    esp32_oled_show_status("TENSION", val);
}

void esp32_oled_show_octave(uint8_t octave)
{
    if (!s_initialized) return;
    char val[4];
    snprintf(val, sizeof(val), "%u", (unsigned)octave);
    esp32_oled_show_status("OCTAVE", val);
}

void esp32_oled_show_transport(bool playing)
{
    if (!s_initialized) return;
    esp32_oled_show_status(playing ? "PLAY" : "STOP", playing ? ">" : "[]");
}

void esp32_oled_show_fx(int fx_mode)
{
    if (!s_initialized) return;
    const char *name = (fx_mode >= 0 && fx_mode < 4) ? s_fx_names[fx_mode] : "?";
    esp32_oled_show_status("FX", name);
}

void esp32_oled_show_wlan(bool on)
{
    if (!s_initialized) return;
    esp32_oled_show_status("WLAN", on ? "ON" : "OFF");
}
