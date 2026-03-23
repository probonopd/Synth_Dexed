/*
 * Step Sequencer Engine
 *
 * Push-style step sequencer with drum and melodic modes.
 * Uses esp_timer for sub-millisecond clock accuracy.
 * Output goes to dexed_raw_handle_midi() -- same path as keyboards.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SEQ_MAX_STEPS           32
#define SEQ_NUM_DRUM_TRACKS     16
#define SEQ_MAX_NOTES_PER_STEP  8
#define SEQ_DEFAULT_BPM         120
#define SEQ_MIN_BPM             20
#define SEQ_MAX_BPM             300

typedef enum {
    SEQ_MODE_DRUM = 0,
    SEQ_MODE_MELODIC,
    SEQ_MODE_BOTH,   /* drum + melodic playing simultaneously */
} seq_mode_t;

/**
 * Initialize the step sequencer.
 * Creates esp_timer (does not start it).
 * @return 0 on success, -1 on failure.
 */
int step_seq_init(void);

/**
 * Deinitialize and free resources.
 */
void step_seq_deinit(void);

/* ---- Transport ---- */
void step_seq_play(void);
void step_seq_stop(void);
void step_seq_toggle_play(void);
bool step_seq_is_playing(void);

/* ---- Mode ---- */
void step_seq_set_mode(seq_mode_t mode);
seq_mode_t step_seq_get_mode(void);

/* ---- BPM ---- */
void step_seq_set_bpm(uint16_t bpm);
void step_seq_adjust_bpm(int16_t delta);
uint16_t step_seq_get_bpm(void);

/* ---- Drum mode ---- */
void step_seq_select_drum(uint8_t drum_index);
uint8_t step_seq_get_selected_drum(void);
void step_seq_drum_toggle_step(uint8_t step);
void step_seq_drum_set_step(uint8_t step, uint8_t velocity);
void step_seq_drum_clear_step(uint8_t step);

/* ---- Velocity ---- */
void step_seq_set_velocity(uint8_t velocity);
uint8_t step_seq_get_velocity(void);

/* ---- Melodic mode ---- */
void step_seq_melodic_toggle_note(uint8_t step, uint8_t midi_note, uint8_t velocity);
void step_seq_melodic_clear_step(uint8_t step);
void step_seq_set_base_octave(uint8_t octave);
uint8_t step_seq_get_base_octave(void);

/* ---- Page navigation ---- */
void step_seq_page_left(void);
void step_seq_page_right(void);
uint8_t step_seq_get_page(void);

/* ---- Input handlers (called from launchpad.cpp) ---- */
void step_seq_handle_grid_press(uint8_t row, uint8_t col, uint8_t velocity);
void step_seq_handle_grid_release(uint8_t row, uint8_t col);
void step_seq_handle_button(uint8_t cc, uint8_t value);

/* ---- State queries for display ---- */
int step_seq_get_current_step(void);
bool step_seq_drum_step_is_active(uint8_t drum, uint8_t step);
uint8_t step_seq_drum_step_velocity(uint8_t drum, uint8_t step);

#ifdef __cplusplus
}
#endif
