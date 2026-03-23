/*
 * Launchpad X Hardware Abstraction
 *
 * Handles: Programmer Mode entry, pad/button coordinate mapping,
 * LED color palette, batch SysEx LED updates.
 *
 * All LED updates are sent via esp32_midi_usb_send_sysex().
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Novation USB identification ---- */
#define NOVATION_VID            0x1235

/* ---- Launchpad X MIDI note mapping (Programmer Mode) ----
 * Note = row*10 + col, where row=1..8 (bottom to top), col=1..8 (left to right)
 */
static inline uint8_t lp_pad_note(uint8_t row, uint8_t col) { return (uint8_t)(row * 10 + col); }
static inline uint8_t lp_note_row(uint8_t note) { return note / 10; }
static inline uint8_t lp_note_col(uint8_t note) { return note % 10; }

/* ---- Quadrant classification ---- */
static inline bool lp_is_grid(uint8_t row, uint8_t col) {
    return row >= 1 && row <= 8 && col >= 1 && col <= 8;
}
static inline bool lp_is_q1(uint8_t row, uint8_t col) {
    return row >= 5 && row <= 8 && col >= 1 && col <= 4;
}
static inline bool lp_is_q2(uint8_t row, uint8_t col) {
    return row >= 5 && row <= 8 && col >= 5 && col <= 8;
}
static inline bool lp_is_q3(uint8_t row, uint8_t col) {
    return row >= 1 && row <= 4 && col >= 1 && col <= 4;
}
static inline bool lp_is_q4(uint8_t row, uint8_t col) {
    return row >= 1 && row <= 4 && col >= 5 && col <= 8;
}

/* ---- Top row button CCs ---- */
#define LP_CC_UP        91
#define LP_CC_DOWN      92
#define LP_CC_LEFT      93
#define LP_CC_RIGHT     94
#define LP_CC_SESSION   95
#define LP_CC_DRUMS     96
#define LP_CC_KEYS      97
#define LP_CC_USER      98

/* ---- Right column button CCs (row 1 bottom to row 8 top) ---- */
#define LP_CC_RIGHT_COL_1   19
#define LP_CC_RIGHT_COL_2   29
#define LP_CC_RIGHT_COL_3   39
#define LP_CC_RIGHT_COL_4   49
#define LP_CC_RIGHT_COL_5   59
#define LP_CC_RIGHT_COL_6   69
#define LP_CC_RIGHT_COL_7   79
#define LP_CC_RIGHT_COL_8   89

/* ---- Launchpad X Color Palette (velocity values) ---- */
#define LP_COLOR_OFF            0
#define LP_COLOR_WHITE_DIM      1
#define LP_COLOR_WHITE          3
#define LP_COLOR_RED_HI         4
#define LP_COLOR_RED            5
#define LP_COLOR_ORANGE         9
#define LP_COLOR_YELLOW         13
#define LP_COLOR_LIME           17
#define LP_COLOR_GREEN          21
#define LP_COLOR_GREEN_HI       22
#define LP_COLOR_SPRING         25
#define LP_COLOR_TURQUOISE      29
#define LP_COLOR_CYAN           33
#define LP_COLOR_SKY            37
#define LP_COLOR_BLUE           41
#define LP_COLOR_PURPLE         45
#define LP_COLOR_MAGENTA        49
#define LP_COLOR_PINK           53
#define LP_COLOR_PEACH          57
#define LP_COLOR_AMBER          96

/* ---- Semantic colors for the sequencer UI ---- */
#define LP_SEQ_PLAYHEAD         LP_COLOR_GREEN
#define LP_SEQ_PLAYHEAD_HIT     LP_COLOR_GREEN_HI
#define LP_SEQ_STEP_ACTIVE      72   /* warm amber */
#define LP_SEQ_SELECTED         LP_COLOR_WHITE
#define LP_SEQ_EMPTY            LP_COLOR_OFF

/* ---- Lifecycle ---- */
void launchpad_init(void);
void launchpad_on_connect(void);
void launchpad_on_disconnect(void);
bool launchpad_is_connected(void);

/* ---- Programmer Mode ---- */
void launchpad_enter_programmer_mode(void);

/* ---- Individual LED control ---- */
void launchpad_set_pad_color(uint8_t note, uint8_t color);
void launchpad_set_cc_color(uint8_t cc, uint8_t color);

/* ---- Batch LED update (efficient SysEx) ----
 * Usage: begin() -> batch_set() calls -> end()
 * end() builds and sends a single SysEx message.
 */
void launchpad_batch_begin(void);
void launchpad_batch_set(uint8_t type, uint8_t led_index, uint8_t color);
void launchpad_batch_end(void);

/* ---- Full display refresh (called by sequencer) ---- */
void launchpad_refresh_grid(void);
void launchpad_clear_all(void);

/* ---- Input dispatch (called from esp32_midi.cpp) ---- */
void launchpad_handle_note(uint8_t note, uint8_t velocity);
void launchpad_handle_cc(uint8_t cc, uint8_t value);

#ifdef __cplusplus
}
#endif
