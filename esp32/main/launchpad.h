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
#define LP_LOGO_NOTE    99   /* Novation logo LED (Programmer Mode, top-right) */

/* ---- Right column button CCs (row 1 bottom to row 8 top) ---- */
#define LP_CC_RIGHT_COL_1   19
#define LP_CC_RIGHT_COL_2   29
#define LP_CC_RIGHT_COL_3   39
#define LP_CC_RIGHT_COL_4   49
#define LP_CC_RIGHT_COL_5   59
#define LP_CC_RIGHT_COL_6   69
#define LP_CC_RIGHT_COL_7   79
#define LP_CC_RIGHT_COL_8   89

/* ---- Right column button function aliases ---- */
#define LP_CC_MODE_SEQ      LP_CC_RIGHT_COL_1   /* Step Sequencer mode */
#define LP_CC_MODE_CIRCLE   LP_CC_RIGHT_COL_2   /* Circle of Fifths mode */
#define LP_CC_MODE_FIELD    LP_CC_RIGHT_COL_3   /* Melodic Field mode */
#define LP_CC_KEY_SELECT    LP_CC_RIGHT_COL_4   /* Cycle through keys */
#define LP_CC_SCALE_SELECT  LP_CC_RIGHT_COL_5   /* Cycle scale types */
#define LP_CC_TENSION       LP_CC_RIGHT_COL_6   /* Cycle tension level */
#define LP_CC_RECORD_PROG   LP_CC_RIGHT_COL_7   /* Record chord progression */
#define LP_CC_CLEAR         LP_CC_RIGHT_COL_8   /* Clear/Reset */

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
#define LP_SEQ_PLAYHEAD         LP_COLOR_WHITE
#define LP_SEQ_PLAYHEAD_HIT     LP_COLOR_WHITE
#define LP_SEQ_STEP_ACTIVE      72   /* warm amber */
#define LP_SEQ_SELECTED         LP_COLOR_WHITE
#define LP_SEQ_EMPTY            LP_COLOR_OFF

/* ---- Semantic colors for harmonic modes ---- */
#define LP_CHORD_ROOT           LP_COLOR_BLUE       /* Root of chord */
#define LP_CHORD_TONE           LP_COLOR_GREEN      /* 3rd, 5th, 7th */
#define LP_SCALE_TONE           LP_COLOR_YELLOW     /* Scale notes */
#define LP_TENSION_TONE         LP_COLOR_ORANGE     /* Tension notes */
#define LP_AVOID_TONE           LP_COLOR_RED        /* Avoid notes */
#define LP_CURRENT_CHORD        LP_COLOR_WHITE      /* Currently selected chord */
#define LP_SUGGESTED_CHORD      LP_COLOR_CYAN       /* Suggested next chord */
#define LP_TONIC_CHORD          LP_COLOR_BLUE       /* Tonic-related chords */
#define LP_DOMINANT_CHORD       LP_COLOR_ORANGE     /* Dominant chords */
#define LP_SUBDOMINANT_CHORD    LP_COLOR_GREEN      /* Subdominant chords */
#define LP_MODE_ACTIVE          LP_COLOR_WHITE      /* Active mode indicator */
#define LP_MODE_INACTIVE        LP_COLOR_WHITE_DIM  /* Inactive mode indicator */

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

/**
 * Show a TSF loading progress bar on the right-column buttons (CCs 19..89).
 *
 *  progress > 0 && < 8 : in-progress — steps 1..N-1 green, step N amber
 *  progress == 8        : all green (fully loaded)
 *  progress < 0         : step-specific failure — -N means step N failed;
 *                         steps 1..N-1 green, step N red, rest off
 *
 * Called once per progress step during boot; after loading ends (success=8
 * or failure<0) the caller must not call this again so the sequencer can
 * update the right column via launchpad_refresh_grid().
 */
void launchpad_show_loading_progress(int progress);

/**
 * Show a binary TSF error code on the right-column buttons (CCs 19..89).
 *
 * Displays the 8-bit error_code as a binary number on the 8 right-column
 * LEDs.  Each LED maps to one TSF_ERR_* flag bit (from tsf_engine.h):
 *
 *   bit  CC   Flag name          Color when SET    Meaning
 *   0    19   FILE_NOT_FOUND     RED               File missing from SPIFFS
 *   1    29   FILE_SIZE          RED               Invalid file size
 *   2    39   PSRAM_ALLOC        RED               No PSRAM for SFO read buffer
 *   3    49   SHORT_READ         RED               Partial file read
 *   4    59   VORBIS_BUF         ORANGE            vorbis workspace alloc failed
 *   5    69   VORBIS_OOM         ORANGE            vorbis bump-allocator overflowed
 *   6    79   VORBIS_DECODE      YELLOW            Corrupt or unsupported OGG data
 *   7    89   TSF_MALLOC         RED               PSRAM exhausted during decode
 *
 * An LED is dim-white when its bit is 0 (that error did not occur),
 * lit in its error color when the bit is 1.
 *
 * After 3 s the led_monitor_task automatically transitions from the
 * step-failure indicator to this binary display.
 * launchpad_refresh_grid() will overwrite it on next sequencer refresh.
 *
 * @param error_code  TSF_ERR_* bitmask from tsf_engine_get_error_detail()
 */
void launchpad_show_tsf_error(uint8_t error_code);

/* ---- Input dispatch (called from esp32_midi.cpp) ---- */
void launchpad_handle_note(uint8_t note, uint8_t velocity);
void launchpad_handle_cc(uint8_t cc, uint8_t value);

#ifdef __cplusplus
}
#endif
