/*
 * chord_guesser.c — Real-time chord recognition from played notes
 *
 * Algorithm: Score-based matching
 * 1. Extract pitch classes from held notes
 * 2. For each possible chord (root 0-11, type 0-15):
 *    - Score = (matching chord tones * 100) - (non-harmonic notes * 25)
 * 3. Return chords sorted by score
 *
 * Time complexity: O(1) — fixed 12 roots × 16 types = 192 checks per call
 */

#include "chord_guesser.h"
#include "step_sequencer.h"
#include "esp_log.h"
#include <stddef.h>
#include <stdio.h>

static const char* TAG = "chord_guesser";

/* Note names for formatting */
static const char* note_names[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

/* Chord type names matching chord_type_t enum */
static const char* chord_type_names[16] = {
    "maj", "min", "7", "m7", "dim", "aug", "sus4", "sus2",
    "maj7", "m/M7", "9", "m9", "maj9", "6", "m6", "?"
};

/* Expanded chord bitmasks (each bit = semitone interval from root) */
static const uint16_t chord_masks[16] = {
    0b000010010001,  /* 0: MAJ      root, M3, P5 */
    0b000010001001,  /* 1: MIN      root, m3, P5 */
    0b010010010001,  /* 2: DOM7     root, M3, P5, m7 */
    0b010010001001,  /* 3: MIN7     root, m3, P5, m7 */
    0b000001001001,  /* 4: DIM      root, m3, dim5 */
    0b000100010001,  /* 5: AUG      root, M3, aug5 */
    0b000010100001,  /* 6: SUS4     root, P4, P5 */
    0b000010000101,  /* 7: SUS2     root, M2, P5 */
    0b100010010001,  /* 8: MAJ7     root, M3, P5, M7 */
    0b100010001001,  /* 9: MIN/MAJ7 root, m3, P5, M7 */
    0b010010110001,  /* 10: DOM9    root, M3, P5, m7, M2 */
    0b010010101001,  /* 11: MIN9    root, m3, P5, m7, M2 */
    0b100010110001,  /* 12: MAJ9    root, M3, P5, M7, M2 */
    0b001010010001,  /* 13: MAJ6    root, M3, P5, M6 */
    0b001010001001,  /* 14: MIN6    root, m3, P5, M6 */
};

/* Diatonic chord qualities for each scale degree in major/minor scales */
/* Index: scale degree 0-6, Value: bitmask of allowed chord types (see chord_type_t) */
static const uint16_t major_scale_chords[7] = {
    /* I chord: maj, maj7, maj6, maj9 */
    (1u << 0) | (1u << 8) | (1u << 12) | (1u << 13),
    /* ii chord: min, min7, min9 */
    (1u << 1) | (1u << 3) | (1u << 11),
    /* iii chord: min, min7, min9 */
    (1u << 1) | (1u << 3) | (1u << 11),
    /* IV chord: maj, maj7, maj6, maj9 */
    (1u << 0) | (1u << 8) | (1u << 12) | (1u << 13),
    /* V chord: maj, dom7, dom9 (dominant 7th-family) */
    (1u << 0) | (1u << 2) | (1u << 10),
    /* vi chord: min, min7, min9 */
    (1u << 1) | (1u << 3) | (1u << 11),
    /* vii° chord: dim */
    (1u << 4),
};

static const uint16_t minor_scale_chords[7] = {
    /* i chord: min, min7, min/maj7, min9 */
    (1u << 1) | (1u << 3) | (1u << 9) | (1u << 11),
    /* ii° chord: dim */
    (1u << 4),
    /* III chord: maj, maj7, maj6, maj9 */
    (1u << 0) | (1u << 8) | (1u << 12) | (1u << 13),
    /* iv chord: min, min7, min9 */
    (1u << 1) | (1u << 3) | (1u << 11),
    /* v chord: min, min7, min9 */
    (1u << 1) | (1u << 3) | (1u << 11),
    /* VI chord: maj, maj7, maj6, maj9 */
    (1u << 0) | (1u << 8) | (1u << 12) | (1u << 13),
    /* VII chord: maj, dom7 */
    (1u << 0) | (1u << 2),
};

/**
 * Convert a pitch class to its scale degree (0-6) within a given scale.
 * Returns -1 if the pitch class is not in the scale.
 * 
 * Example: In C major, G (pitch class 7) is scale degree 4.
 */
static int pitch_class_to_scale_degree(uint8_t pitch_class, uint8_t key, uint8_t scale_type)
{
    if (scale_type >= 9) return -1;  /* unknown scale */
    
    /* The intervals in semitones for each scale degree relative to root */
    static const int major_intervals[7] = {0, 2, 4, 5, 7, 9, 11};
    static const int minor_intervals[7] = {0, 2, 3, 5, 7, 8, 10};
    
    const int* intervals = (scale_type == 0) ? major_intervals : minor_intervals;
    int relative_pitch = (pitch_class - key + 12) % 12;
    
    for (int degree = 0; degree < 7; degree++) {
        if (intervals[degree] == relative_pitch) {
            return degree;
        }
    }
    return -1;  /* Not in scale */
}

/**
 * Check if a chord (root + type) is valid in the given scale.
 * Now takes the absolute root (0-11) and key, and computes the scale degree.
 */
static bool is_diatonic_chord(uint8_t root, uint8_t chord_type,
                              uint8_t key, uint8_t scale_type)
{
    int scale_degree = pitch_class_to_scale_degree(root, key, scale_type);
    if (scale_degree < 0) return false;  /* Root not in scale */
    
    uint16_t type_bit = (1u << chord_type);
    
    if (scale_type == 0) {  /* MAJOR */
        return (major_scale_chords[scale_degree] & type_bit) != 0;
    } else if (scale_type == 1) {  /* MINOR */
        return (minor_scale_chords[scale_degree] & type_bit) != 0;
    }
    
    /* For other scales, just check if root is diatonic */
    return (scale_degree >= 0);
}

int chord_guesser_init(void)
{
    ESP_LOGI(TAG, "Chord guesser initialized");
    return 0;
}

/**
 * Score how well a given chord matches the played notes.
 * Higher score = better match.
 * @param pitch_classes: bitmask of active pitch classes (bits 0-11)
 * @param chord_mask: bitmask of chord tones
 * @param root: which root this chord has (0-11)
 * @return: score 0-255
 */
static uint8_t score_chord(uint16_t pitch_classes, uint16_t chord_mask, uint8_t root)
{
    /* REQUIRE root note to be present - otherwise this chord isn't being played */
    if (!((pitch_classes >> root) & 1)) {
        return 0;
    }
    
    /* Rotate chord mask to root position */
    uint16_t rotated = 0;
    for (int i = 0; i < 12; i++) {
        if ((chord_mask >> i) & 1) {
            rotated |= (1 << ((i + root) % 12));
        }
    }
    
    /* Count chord tones (bits in the chord) */
    int chord_size = 0;
    for (int i = 0; i < 12; i++) {
        if ((chord_mask >> i) & 1) chord_size++;
    }
    
    /* Count matching chord tones */
    int matches = 0;
    for (int i = 0; i < 12; i++) {
        if ((pitch_classes >> i) & 1 && (rotated >> i) & 1) {
            matches++;
        }
    }
    
    /* Count non-harmonic notes (played but not in chord) */
    int non_harmonic = 0;
    for (int i = 0; i < 12; i++) {
        if ((pitch_classes >> i) & 1 && !((rotated >> i) & 1)) {
            non_harmonic++;
        }
    }
    
    int missing_tones = chord_size - matches;
    
    /* Scoring: 
     * - Base: matches * 40 (moderate per-tone reward)
     * - HUGE bonus for perfect voicing (all chord tones present)
     * - Penalty for missing tones and extra notes
     */
    int score = (matches * 40);
    
    if (missing_tones == 0 && non_harmonic == 0) {
        /* PERFECT: all chord tones present, no extras */
        score += 120;
    } else if (missing_tones == 0) {
        /* Complete chord but with extra notes */
        score += 80;
    }
    
    /* Penalties */
    score -= (missing_tones * 50);
    score -= (non_harmonic * 60);
    
    if (score < 0) score = 0;
    if (score > 255) score = 255;
    
    return (uint8_t)score;
}

int chord_guesser_analyze(const bool active_notes[128],
                          const harmonic_state_t* harmonic,
                          chord_guess_t* out_guesses, 
                          int max_guesses)
{
    if (!out_guesses || max_guesses < 1) return 0;
    
    /* Extract pitch classes from active notes */
    uint16_t pitch_classes = 0;
    int active_pitch_count = 0;
    for (int note = 0; note < 128; note++) {
        if (active_notes[note]) {
            int pc = note % 12;
            if (!((pitch_classes >> pc) & 1)) {
                active_pitch_count++;
            }
            pitch_classes |= (1 << pc);
        }
    }
    
    /* Require at least 2 pitch classes for a valid chord (allows intervals, power chords, etc.) */
    if (active_pitch_count < 2) return 0;
    
    /* No notes playing */
    if (pitch_classes == 0) return 0;
    
    /* Score all possible chords */
    typedef struct { uint8_t root; uint8_t type; uint8_t score; } scored_chord_t;
    scored_chord_t scores[192];  /* 12 roots × 16 types */
    int score_count = 0;
    
    for (int root = 0; root < 12; root++) {
        for (int type = 0; type < 16; type++) {
            uint8_t score = score_chord(pitch_classes, chord_masks[type], root);
            /* Require minimum score of 100 for inclusion (higher confidence) */
            if (score >= 100) {
                /* Apply diatonic context boost if harmonic state provided */
                if (harmonic) {
                    if (is_diatonic_chord(root, type, harmonic->key, harmonic->scale_type)) {
                        /* Boost diatonic chords significantly (add 60 points) */
                        int boosted = (int)score + 60;
                        if (boosted > 255) boosted = 255;
                        score = (uint8_t)boosted;
                    }
                }
                
                scores[score_count].root = root;
                scores[score_count].type = type;
                scores[score_count].score = score;
                score_count++;
            }
        }
    }
    
    /* Sort by score (bubble sort, small array) */
    for (int i = 0; i < score_count; i++) {
        for (int j = i + 1; j < score_count; j++) {
            if (scores[j].score > scores[i].score) {
                scored_chord_t tmp = scores[i];
                scores[i] = scores[j];
                scores[j] = tmp;
            }
        }
    }
    
    /* Output top matches */
    int out_count = (score_count < max_guesses) ? score_count : max_guesses;
    for (int i = 0; i < out_count; i++) {
        out_guesses[i].root = scores[i].root;
        out_guesses[i].type = scores[i].type;
        out_guesses[i].score = scores[i].score;
    }
    
    return out_count;
}

int chord_guesser_get_best(const bool active_notes[128],
                           uint8_t* out_root,
                           uint8_t* out_type)
{
    chord_guess_t guess;
    int count = chord_guesser_analyze(active_notes, NULL, &guess, 1);
    if (count > 0 && out_root && out_type) {
        *out_root = guess.root;
        *out_type = guess.type;
        return 0;
    }
    return -1;
}
