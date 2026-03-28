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
    SEQ_MODE_BOTH,      /* drum + melodic playing simultaneously */
    SEQ_MODE_CIRCLE,    /* Circle of Fifths chord progression mode */
    SEQ_MODE_FIELD,     /* Melodic field mode - notes guided by current chord */
} seq_mode_t;

/* Chord types */
typedef enum {
    CHORD_MAJ = 0,
    CHORD_MIN,
    CHORD_DOM7,
    CHORD_MIN7,
    CHORD_DIM,
    CHORD_AUG,
    CHORD_SUS4,
    CHORD_SUS2,
    CHORD_TYPE_COUNT
} chord_type_t;

/* Scale types */
typedef enum {
    SCALE_MAJOR = 0,
    SCALE_MINOR,
    SCALE_DORIAN,
    SCALE_MIXOLYDIAN,
    SCALE_PHRYGIAN,
    SCALE_LYDIAN,
    SCALE_LOCRIAN,
    SCALE_PENTATONIC_MAJ,
    SCALE_PENTATONIC_MIN,
    SCALE_TYPE_COUNT
} scale_type_t;

/* Harmonic state - tracks current key, chord, and scale context */
typedef struct {
    uint8_t key;           /* 0-11 (C=0, C#=1, ... B=11) */
    uint8_t chord_root;    /* 0-11 relative to key */
    chord_type_t chord_type;
    scale_type_t scale_type;
    uint8_t tension;       /* 0-3 tension level affecting note selection */
    uint8_t prev_chord_root;   /* previous chord root (for suggestion engine) */
    chord_type_t prev_chord_type;
    bool has_prev_chord;       /* true after first chord press */
} harmonic_state_t;

/**
 * Initialize the step sequencer.
 * Creates esp_timer (does not start it).
 * @return 0 on success, -1 on failure.
 */

/* FIELD-page lookahead: how many milliseconds before a chord change the display
 * (and note quantizer) should already show the upcoming chord. Adjust to taste.
 * This is a fixed wall-clock window — independent of BPM. */
#define SEQ_FIELD_LOOKAHEAD_MS  250

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
/* Returns true once when an outro fill has finished (main task should call step_seq_stop). */
bool step_seq_consume_outro_fill_done(void);

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

/* ---- Harmonic state (Circle/Field modes) ---- */
void step_seq_set_key(uint8_t key);
uint8_t step_seq_get_key(void);
void step_seq_set_chord(uint8_t root, chord_type_t type);
uint8_t step_seq_get_chord_root(void);
chord_type_t step_seq_get_chord_type(void);
void step_seq_set_scale(scale_type_t scale);
scale_type_t step_seq_get_scale(void);
void step_seq_set_tension(uint8_t tension);
uint8_t step_seq_get_tension(void);
const harmonic_state_t* step_seq_get_harmonic_state(void);

/* Suggestion engine: score how well to_root follows the last chord (0-5). */
uint8_t step_seq_get_suggestion_score(uint8_t to_root);

/* Field mode note quantizer: snaps non-lit notes to next lower lit note.
 * Apply to external Note On only — see below for note-off. */
uint8_t step_seq_field_quantize_note(uint8_t midi_note);

/* Field mode note remap table: call record_noteon after every quantized note-on
 * so that resolve_noteoff can return the correct playing note for the release,
 * independent of any subsequent harmonic state changes. */
void    step_seq_field_record_noteon(uint8_t raw_note, uint8_t quantized_note);
uint8_t step_seq_field_resolve_noteoff(uint8_t raw_note);

/* Lookahead harmonic state for FIELD display: previews the next chord 1/16-note early
 * while the sequencer is running.  Identical to step_seq_get_harmonic_state() when stopped. */
const harmonic_state_t* step_seq_get_display_harmonic_state(void);

/* Melodic mode velocity scaling: scales velocity by harmonic role (chord/scale/tension).
 * Returns velocity unchanged when not in MELODIC mode. */
uint8_t step_seq_melodic_scale_velocity(uint8_t midi_note, uint8_t velocity);

/* Field mode: get the MIDI note mapped to a grid position (row/col 1-8). */
uint8_t step_seq_get_field_note(uint8_t row, uint8_t col);

/* Check if a pitch class is a chord/scale tone for display use. */
bool step_seq_is_chord_tone(uint8_t pitch_class, uint8_t chord_root, chord_type_t chord_type);

/* Writes Roman numeral representation of chord (e.g. "V", "vi", "bVII") into out[len]. */
void step_seq_chord_roman_numeral(uint8_t chord_root_rel, chord_type_t type,
                                  scale_type_t scale, char *out, int len);
bool step_seq_is_scale_tone(uint8_t pitch_class, uint8_t key, scale_type_t scale_type);

/* Get the chord root (semitone) for a circle grid position. */
void step_seq_get_circle_chord(uint8_t row, uint8_t col, uint8_t* out_root, chord_type_t* out_type);

/* ---- Chord step sequencer (Circle mode, rows 5-8) ---- */
bool         step_seq_chord_seq_step_active(uint8_t step);  /* is step programmed? */
uint8_t      step_seq_chord_seq_step_root(uint8_t step);    /* absolute root semitone */
chord_type_t step_seq_chord_seq_step_type(uint8_t step);    /* chord quality */
int          step_seq_chord_seq_current(void);               /* current playing step (0-7) or -1 */

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
