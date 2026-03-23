/*
 * Launchpad X Hardware Abstraction
 *
 * Implements Programmer Mode management, batch SysEx LED updates,
 * coordinate mapping, and input dispatch to the step sequencer.
 */

#include "launchpad.h"
#include "step_sequencer.h"
#include "esp32_midi.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "launchpad";

/* ---- SysEx messages ---- */
static const uint8_t sysex_programmer_mode[] = {
    0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x01, 0xF7
};

/* ---- Connection state ---- */
static volatile bool s_connected = false;

/* ---- Batch SysEx buffer ----
 * Format: F0 00 20 29 02 0C 03 [type LED color]... F7
 * Header = 7 bytes, each entry = 3 bytes, footer = 1 byte
 * Max ~80 entries = 7 + 240 + 1 = 248 bytes
 */
#define LP_BATCH_HEADER_LEN  7
#define LP_BATCH_MAX_ENTRIES 80
#define LP_BATCH_BUF_SIZE    (LP_BATCH_HEADER_LEN + LP_BATCH_MAX_ENTRIES * 3 + 1)

static uint8_t s_batch_buf[LP_BATCH_BUF_SIZE];
static int s_batch_len = 0;

/* ---- Drum color palette (one per track) ---- */
static const uint8_t drum_colors[SEQ_NUM_DRUM_TRACKS] = {
     5,  /*  0: Red          (Kick)          */
     9,  /*  1: Orange       (Side Stick)    */
    13,  /*  2: Yellow       (Snare)         */
    17,  /*  3: Lime         (Clap)          */
    21,  /*  4: Green        (Elec Snare)    */
    25,  /*  5: Spring       (Lo Floor Tom)  */
    29,  /*  6: Turquoise    (Closed HH)     */
    33,  /*  7: Cyan         (Hi Floor Tom)  */
    37,  /*  8: Sky          (Pedal HH)      */
    41,  /*  9: Blue         (Low Tom)       */
    45,  /* 10: Purple       (Open HH)       */
    49,  /* 11: Magenta      (Lo-Mid Tom)    */
    53,  /* 12: Pink         (Hi-Mid Tom)    */
    57,  /* 13: Peach        (Crash)         */
    96,  /* 14: Amber        (High Tom)      */
    72,  /* 15: Warm amber   (Ride)          */
};

/* ---- Velocity levels for Q4 ---- */
static const uint8_t velocity_levels[16] = {
    8, 16, 24, 32, 40, 48, 56, 64,
    72, 80, 88, 96, 104, 112, 120, 127
};

/* ================================================
 * Lifecycle
 * ================================================ */

void launchpad_init(void)
{
    s_connected = false;
    ESP_LOGI(TAG, "Launchpad abstraction initialized");
}

void launchpad_on_connect(void)
{
    s_connected = true;
    ESP_LOGI(TAG, "Launchpad connected -- entering Programmer Mode");
    launchpad_enter_programmer_mode();

    /* Brief delay for the Launchpad to process the SysEx */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Show initial display */
    launchpad_refresh_grid();
}

void launchpad_on_disconnect(void)
{
    s_connected = false;
    ESP_LOGI(TAG, "Launchpad disconnected");
}

bool launchpad_is_connected(void)
{
    return s_connected;
}

/* ================================================
 * Programmer Mode
 * ================================================ */

void launchpad_enter_programmer_mode(void)
{
    esp32_midi_usb_send_sysex(sysex_programmer_mode, sizeof(sysex_programmer_mode));
}

/* ================================================
 * Individual LED control
 * ================================================ */

void launchpad_set_pad_color(uint8_t note, uint8_t color)
{
    /* Static color: Note On, channel 1 (status 0x90) */
    esp32_midi_usb_send_msg(0x90, note, color);
}

void launchpad_set_cc_color(uint8_t cc, uint8_t color)
{
    /* CC on channel 1 (status 0xB0) */
    esp32_midi_usb_send_msg(0xB0, cc, color);
}

/* ================================================
 * Batch LED update via SysEx
 * ================================================ */

void launchpad_batch_begin(void)
{
    s_batch_buf[0] = 0xF0;
    s_batch_buf[1] = 0x00;
    s_batch_buf[2] = 0x20;
    s_batch_buf[3] = 0x29;
    s_batch_buf[4] = 0x02;
    s_batch_buf[5] = 0x0C;
    s_batch_buf[6] = 0x03;
    s_batch_len = LP_BATCH_HEADER_LEN;
}

void launchpad_batch_set(uint8_t type, uint8_t led_index, uint8_t color)
{
    if (s_batch_len + 3 >= LP_BATCH_BUF_SIZE - 1) return;
    s_batch_buf[s_batch_len++] = type;
    s_batch_buf[s_batch_len++] = led_index;
    s_batch_buf[s_batch_len++] = color;
}

void launchpad_batch_end(void)
{
    if (s_batch_len <= LP_BATCH_HEADER_LEN) return;
    s_batch_buf[s_batch_len++] = 0xF7;
    esp32_midi_usb_send_sysex(s_batch_buf, s_batch_len);
}

/* ================================================
 * Clear all LEDs
 * ================================================ */

void launchpad_clear_all(void)
{
    launchpad_batch_begin();
    for (uint8_t row = 1; row <= 8; row++) {
        for (uint8_t col = 1; col <= 8; col++) {
            launchpad_batch_set(0, lp_pad_note(row, col), LP_COLOR_OFF);
        }
    }
    /* Top row buttons */
    for (uint8_t cc = 91; cc <= 98; cc++) {
        launchpad_batch_set(0, cc, LP_COLOR_OFF);
    }
    /* Right column buttons */
    static const uint8_t right_ccs[] = {19, 29, 39, 49, 59, 69, 79, 89};
    for (int i = 0; i < 8; i++) {
        launchpad_batch_set(0, right_ccs[i], LP_COLOR_OFF);
    }
    launchpad_batch_end();
}

/* ================================================
 * TSF loading progress bar on the right column
 * Bottom cell (row 1, CC 19) = step 1, top (row 8, CC 89) = step 8.
 * Green = done, Amber = current, Off = pending, Red = failed.
 * ================================================ */

void launchpad_show_loading_progress(int progress)
{
    if (!s_connected) return;
    static const uint8_t right_ccs[] = {19, 29, 39, 49, 59, 69, 79, 89};
    const int steps = 8;

    launchpad_batch_begin();
    for (int i = 0; i < steps; i++) {
        uint8_t color;
        if (progress < 0) {
            /* Step-specific error: -N means step N failed.
             * Show steps 1..(N-1) green, step N red, rest off. */
            int failed_step = -progress;
            if (i < failed_step - 1) {
                color = LP_COLOR_GREEN;
            } else if (i == failed_step - 1) {
                color = LP_COLOR_RED;
            } else {
                color = LP_COLOR_OFF;
            }
        } else if (progress >= 8 || i < progress - 1) {
            /* Completed steps (or all done = all green) */
            color = LP_COLOR_GREEN;
        } else if (i == progress - 1) {
            /* Current step in progress */
            color = LP_COLOR_AMBER;
        } else {
            color = LP_COLOR_OFF;
        }
        launchpad_batch_set(0, right_ccs[i], color);
    }
    launchpad_batch_end();
}

/* ================================================
 * TSF error code display — binary encoding on right column
 *
 * bit 0 (LSB) = bottom LED (CC 19), bit 7 (MSB) = top LED (CC 89)
 *
 * Color per bit encodes the error class at a glance:
 *   bit 0  CC 19  FILE_NOT_FOUND  → RED    (fatal I/O error)
 *   bit 1  CC 29  FILE_SIZE       → RED    (fatal I/O error)
 *   bit 2  CC 39  PSRAM_ALLOC     → RED    (out of PSRAM)
 *   bit 3  CC 49  SHORT_READ      → RED    (partial read)
 *   bit 4  CC 59  VORBIS_BUF      → ORANGE (workspace alloc failed)
 *   bit 5  CC 69  VORBIS_OOM      → ORANGE (bump-allocator overflow)
 *   bit 6  CC 79  VORBIS_DECODE   → YELLOW (corrupt OGG data)
 *   bit 7  CC 89  TSF_MALLOC      → RED    (PSRAM exhausted during decode)
 *   any    —      bit clear       → dim white (shows '0' in binary)
 * ================================================ */

/* Error color per bit — must match TSF_ERR_* bit order in tsf_engine.h */
static const uint8_t kTsfErrColor[8] = {
    LP_COLOR_RED,     /* bit 0: TSF_ERR_FILE_NOT_FOUND */
    LP_COLOR_RED,     /* bit 1: TSF_ERR_FILE_SIZE      */
    LP_COLOR_RED,     /* bit 2: TSF_ERR_PSRAM_ALLOC    */
    LP_COLOR_RED,     /* bit 3: TSF_ERR_SHORT_READ     */
    LP_COLOR_ORANGE,  /* bit 4: TSF_ERR_VORBIS_BUF     */
    LP_COLOR_ORANGE,  /* bit 5: TSF_ERR_VORBIS_OOM     */
    LP_COLOR_YELLOW,  /* bit 6: TSF_ERR_VORBIS_DECODE  */
    LP_COLOR_RED,     /* bit 7: TSF_ERR_TSF_MALLOC     */
};

static const char * const kTsfErrName[8] = {
    "FILE_NOT_FOUND",
    "FILE_SIZE",
    "PSRAM_ALLOC",
    "SHORT_READ",
    "VORBIS_BUF",
    "VORBIS_OOM",
    "VORBIS_DECODE",
    "TSF_MALLOC",
};

void launchpad_show_tsf_error(uint8_t error_code)
{
    if (!s_connected) return;
    static const uint8_t right_ccs[] = {19, 29, 39, 49, 59, 69, 79, 89};

    ESP_LOGI(TAG, "TSF error 0x%02X on right column LEDs (bit0=CC19 .. bit7=CC89):", error_code);
    for (int i = 0; i < 8; i++) {
        bool bit_set = (error_code >> i) & 1;
        ESP_LOGI(TAG, "  bit%d  CC%2d  %-18s: %s",
                 i, right_ccs[i], kTsfErrName[i],
                 bit_set ? "SET" : "clear");
    }

    launchpad_batch_begin();
    for (int i = 0; i < 8; i++) {
        bool bit_set = (error_code >> i) & 1;
        uint8_t color = bit_set ? kTsfErrColor[i] : LP_COLOR_WHITE_DIM;
        launchpad_batch_set(0, right_ccs[i], color);
    }
    launchpad_batch_end();
}

/* ================================================
 * Coordinate mapping helpers
 * ================================================ */

/* Q1/Q2 pad → step index (Drum mode).
 * Row 8=steps 0-7, Row 7=8-15, Row 6=16-23, Row 5=24-31 */
static int grid_to_step_index(uint8_t row, uint8_t col)
{
    if (row < 5 || row > 8 || col < 1 || col > 8) return -1;
    int row_offset = (8 - row) * 8;
    return row_offset + (col - 1);
}

/* Q3 pad → drum index (0-15).
 * Row 1 cols 1-4 = drums 0-3, Row 2 = 4-7, etc. */
static int q3_to_drum_index(uint8_t row, uint8_t col)
{
    if (row < 1 || row > 4 || col < 1 || col > 4) return -1;
    return (row - 1) * 4 + (col - 1);
}

/* Q4 pad → velocity index (0-15).
 * Same layout as Q3 but in cols 5-8. */
static int q4_to_velocity_index(uint8_t row, uint8_t col)
{
    if (row < 1 || row > 4 || col < 5 || col > 8) return -1;
    return (row - 1) * 4 + (col - 5);
}

/* Melodic mode: keyboard pad to MIDI note */
static uint8_t keyboard_pad_to_midi_note(uint8_t row, uint8_t col)
{
    uint8_t base = step_seq_get_base_octave() * 12;
    int offset;
    if (col <= 4) {
        offset = (row - 1) * 4 + (col - 1);
    } else {
        offset = 16 + (row - 1) * 4 + (col - 5);
    }
    uint8_t note = (uint8_t)(base + offset);
    return note > 127 ? 127 : note;
}

/* ================================================
 * Full grid refresh
 * ================================================ */

void launchpad_refresh_grid(void)
{
    if (!s_connected) return;

    seq_mode_t mode = step_seq_get_mode();
    int playhead = step_seq_get_current_step();
    uint8_t sel_drum = step_seq_get_selected_drum();

    launchpad_batch_begin();

    if (mode == SEQ_MODE_DRUM || mode == SEQ_MODE_BOTH) {
        /* ---- Q1 + Q2: Step grid for selected drum ---- */
        for (uint8_t row = 5; row <= 8; row++) {
            for (uint8_t col = 1; col <= 8; col++) {
                int step = grid_to_step_index(row, col);
                uint8_t color = LP_COLOR_OFF;

                bool active = step_seq_drum_step_is_active(sel_drum, (uint8_t)step);

                if (step == playhead) {
                    color = active ? LP_SEQ_PLAYHEAD_HIT : LP_SEQ_PLAYHEAD;
                } else if (active) {
                    color = drum_colors[sel_drum];
                }

                launchpad_batch_set(0, lp_pad_note(row, col), color);
            }
        }

        /* ---- Q3: Drum pads ---- */
        for (uint8_t row = 1; row <= 4; row++) {
            for (uint8_t col = 1; col <= 4; col++) {
                int drum = q3_to_drum_index(row, col);
                uint8_t color;
                if (drum == sel_drum) {
                    color = LP_COLOR_WHITE;
                } else {
                    bool has_steps = false;
                    for (int s = 0; s < SEQ_MAX_STEPS; s++) {
                        if (step_seq_drum_step_is_active((uint8_t)drum, (uint8_t)s)) {
                            has_steps = true;
                            break;
                        }
                    }
                    color = has_steps ? drum_colors[drum] : LP_COLOR_WHITE_DIM;
                }
                launchpad_batch_set(0, lp_pad_note(row, col), color);
            }
        }

        /* ---- Q4: Velocity pads ---- */
        uint8_t cur_vel = step_seq_get_velocity();
        for (uint8_t row = 1; row <= 4; row++) {
            for (uint8_t col = 5; col <= 8; col++) {
                int vel_idx = q4_to_velocity_index(row, col);
                uint8_t this_vel = velocity_levels[vel_idx];
                uint8_t color;
                if (this_vel == cur_vel) {
                    color = LP_COLOR_WHITE;
                } else if (vel_idx < 4) {
                    color = LP_COLOR_RED;
                } else if (vel_idx < 8) {
                    color = LP_COLOR_ORANGE;
                } else if (vel_idx < 12) {
                    color = LP_COLOR_YELLOW;
                } else {
                    color = LP_COLOR_GREEN;
                }
                launchpad_batch_set(0, lp_pad_note(row, col), color);
            }
        }
    }
    else if (mode == SEQ_MODE_MELODIC) {
        /* ---- Q1+Q2: Melodic step grid ----
         * Columns = time steps (col 1-8 = steps 0-7)
         * Rows = pitch (row 5 = lowest, row 8 = highest visible) */
        uint8_t base_oct = step_seq_get_base_octave();
        (void)base_oct;
        for (uint8_t row = 5; row <= 8; row++) {
            for (uint8_t col = 1; col <= 8; col++) {
                int step = col - 1;
                uint8_t color = LP_COLOR_OFF;

                if (step == playhead) {
                    color = LP_SEQ_PLAYHEAD;
                }

                launchpad_batch_set(0, lp_pad_note(row, col), color);
            }
        }

        /* ---- Q3+Q4: Keyboard ---- */
        for (uint8_t row = 1; row <= 4; row++) {
            for (uint8_t col = 1; col <= 8; col++) {
                uint8_t note = keyboard_pad_to_midi_note(row, col);
                /* White keys vs black keys coloring */
                int pc = note % 12;
                bool is_black = (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10);
                uint8_t color = is_black ? LP_COLOR_BLUE : LP_COLOR_WHITE_DIM;
                launchpad_batch_set(0, lp_pad_note(row, col), color);
            }
        }
    }

    /* ---- Top row buttons ---- */
    launchpad_batch_set(0, LP_CC_UP,      LP_COLOR_YELLOW);
    launchpad_batch_set(0, LP_CC_DOWN,    LP_COLOR_YELLOW);
    launchpad_batch_set(0, LP_CC_LEFT,    LP_COLOR_SKY);
    launchpad_batch_set(0, LP_CC_RIGHT,   LP_COLOR_SKY);
    launchpad_batch_set(0, LP_CC_SESSION,
        step_seq_is_playing() ? LP_COLOR_GREEN : LP_COLOR_RED);
    launchpad_batch_set(0, LP_CC_DRUMS,
        (mode == SEQ_MODE_DRUM || mode == SEQ_MODE_BOTH) ? LP_COLOR_WHITE : LP_COLOR_WHITE_DIM);
    launchpad_batch_set(0, LP_CC_KEYS,
        (mode == SEQ_MODE_MELODIC || mode == SEQ_MODE_BOTH) ? LP_COLOR_WHITE : LP_COLOR_WHITE_DIM);
    launchpad_batch_set(0, LP_CC_USER, LP_COLOR_OFF);

    launchpad_batch_end();
}

/* ================================================
 * Input dispatch
 * ================================================ */

void launchpad_handle_note(uint8_t note, uint8_t velocity)
{
    uint8_t row = lp_note_row(note);
    uint8_t col = lp_note_col(note);

    if (!lp_is_grid(row, col)) return;

    if (velocity > 0) {
        step_seq_handle_grid_press(row, col, velocity);
    } else {
        step_seq_handle_grid_release(row, col);
    }
}

void launchpad_handle_cc(uint8_t cc, uint8_t value)
{
    /* Forward both press (value>0) and release (value==0) so that
     * step_seq_handle_button can track button-hold state for SEQ_MODE_BOTH. */
    step_seq_handle_button(cc, value);
}
