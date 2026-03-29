/*
 * chord_guesser.h — Real-time chord recognition from played notes
 *
 * Analyzes currently held MIDI notes and identifies matching chord(s).
 * Useful for displaying chord names while jamming on the keyboard.
 *
 * Features:
 * - Works with any voicing (inversions, extended notes, etc.)
 * - Returns scored suggestions (best match first)
 * - Harmonic context aware (uses current key/scale)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "step_sequencer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t root;      /* 0-11: pitch class of root */
    uint8_t type;      /* chord_type_t */
    uint8_t score;     /* 0-255: quality of match */
} chord_guess_t;

/** Initialize chord guesser */
int chord_guesser_init(void);

/** 
 * Analyze held notes and return chord guesses.
 * @param active_notes: array of 128 bools (one per MIDI note)
 * @param harmonic: harmonic context (key, scale) to prefer diatonic chords
 * @param out_guesses: output array (max 3 suggestions)
 * @param max_guesses: size of output array
 * @return: number of guesses populated (0-max_guesses)
 */
int chord_guesser_analyze(const bool active_notes[128],
                          const harmonic_state_t* harmonic,
                          chord_guess_t* out_guesses, 
                          int max_guesses);

/** Get best single chord guess, or -1 if no match */
int chord_guesser_get_best(const bool active_notes[128],
                           uint8_t* out_root,
                           uint8_t* out_type);

#ifdef __cplusplus
}
#endif
