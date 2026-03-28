/*
 * Step Sequencer Engine
 *
 * Push-style step sequencer with drum and melodic modes.
 * Uses esp_timer for high-resolution periodic clock.
 * All note output feeds dexed_raw_handle_midi() via the existing MIDI queue.
 */

#include "step_sequencer.h"
#include "launchpad.h"
#include "esp32_oled.h"
#include "dexed_raw.h"
#include "tsf_engine.h"
#include "esp32_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "step_seq";

/* ================================================
 * Constants
 * ================================================ */

#define SEQ_DEFAULT_VELOCITY    100
#define SEQ_ACTIVE_NOTES_MAX    32

/* Drum MIDI note table (GM-ish: C2=36 through D#3=51) */
static const uint8_t drum_midi_notes[SEQ_NUM_DRUM_TRACKS] = {
    36, 37, 38, 39, 40, 41, 42, 43,
    44, 45, 46, 47, 48, 49, 50, 51,
};

/* ================================================
 * Harmonic Mode Constants & Lookup Tables
 * ================================================ */

/* Circle of Fifths: index = position on circle, value = semitone from C */
static const uint8_t circle_to_semitone[12] = {
    0,  /* C  - position 0 */
    7,  /* G  - position 1 */
    2,  /* D  - position 2 */
    9,  /* A  - position 3 */
    4,  /* E  - position 4 */
    11, /* B  - position 5 */
    6,  /* F#/Gb - position 6 */
    1,  /* C#/Db - position 7 */
    8,  /* G#/Ab - position 8 */
    3,  /* D#/Eb - position 9 */
    10, /* A#/Bb - position 10 */
    5,  /* F  - position 11 */
};

/* Reverse lookup: semitone to circle position */
static const uint8_t semitone_to_circle[12] = {
    0,  /* C = position 0 */
    7,  /* C# = position 7 */
    2,  /* D = position 2 */
    9,  /* D# = position 9 */
    4,  /* E = position 4 */
    11, /* F = position 11 */
    6,  /* F# = position 6 */
    1,  /* G = position 1 */
    8,  /* G# = position 8 */
    3,  /* A = position 3 */
    10, /* A# = position 10 */
    5,  /* B = position 5 */
};

/* Chord intervals as bitmasks (12 bits for chromatic notes) */
static const uint16_t chord_masks[CHORD_TYPE_COUNT] = {
    0b000010010001,  /* MAJ:  root, M3, P5 */
    0b000010001001,  /* MIN:  root, m3, P5 */
    0b010010010001,  /* DOM7: root, M3, P5, m7 */
    0b010010001001,  /* MIN7: root, m3, P5, m7 */
    0b000001001001,  /* DIM:  root, m3, dim5 */
    0b000100010001,  /* AUG:  root, M3, aug5 */
    0b000010100001,  /* SUS4: root, P4, P5 */
    0b000010000101,  /* SUS2: root, M2, P5 */
};

/* Scale intervals as bitmasks */
static const uint16_t scale_masks[SCALE_TYPE_COUNT] = {
    0b101010110101,  /* MAJOR:      W-W-H-W-W-W-H */
    0b010110101101,  /* MINOR:      W-H-W-W-H-W-W */
    0b010110101011,  /* DORIAN:     W-H-W-W-W-H-W */
    0b010010110101,  /* MIXOLYDIAN: W-W-H-W-W-H-W */
    0b010110011011,  /* PHRYGIAN:   H-W-W-W-H-W-W */
    0b101010110011,  /* LYDIAN:     W-W-W-H-W-W-H */
    0b010101101011,  /* LOCRIAN:    H-W-W-H-W-W-W */
    0b001010010101,  /* PENTA_MAJ:  W-W-m3-W-m3 */
    0b010010101001,  /* PENTA_MIN:  m3-W-W-m3-W */
};

/* Note names for debugging */
static const char* note_names[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

/* Chord type names for debugging */
static const char* chord_type_names[CHORD_TYPE_COUNT] = {
    "maj", "min", "7", "m7", "dim", "aug", "sus4", "sus2"
};

/* ================================================
 * Data Structures
 * ================================================ */

typedef struct {
    uint8_t velocity;   /* 0 = off, 1-127 = on */
} seq_drum_step_t;

typedef struct {
    seq_drum_step_t steps[SEQ_MAX_STEPS];
} seq_drum_track_t;

typedef struct {
    uint8_t notes[SEQ_MAX_NOTES_PER_STEP];
    uint8_t velocities[SEQ_MAX_NOTES_PER_STEP];
    uint8_t note_count;
} seq_melodic_step_t;

typedef struct {
    /* Pattern data */
    seq_drum_track_t    drum_tracks[SEQ_NUM_DRUM_TRACKS];
    seq_melodic_step_t  melodic_steps[SEQ_MAX_STEPS];

    /* Transport */
    seq_mode_t  mode;
    bool        playing;
    int         current_step;   /* 0..num_steps-1, -1 when stopped */
    uint8_t     num_steps;      /* active loop length */

    /* Drum mode UI state */
    uint8_t     selected_drum;
    uint8_t     current_velocity;

    /* Melodic mode UI state */
    uint8_t     base_octave;

    /* Harmonic mode state */
    harmonic_state_t harmonic;
    harmonic_state_t display_harmonic; /* lookahead: previews next chord SEQ_FIELD_LOOKAHEAD_MS early */

    /* Clock */
    uint16_t    bpm;
    esp_timer_handle_t timer;

    /* Active note tracking for Note Off scheduling */
    struct {
        uint8_t note;
        uint8_t channel;
    } active_notes[SEQ_ACTIVE_NOTES_MAX];
    int active_note_count;

    /* Field mode: currently held notes for proper note-off */
    struct {
        uint8_t note;
        bool    active;
    } field_notes[64];

    /* Circle mode: currently held chord (sustain until toggled off or new chord pressed) */
    struct {
        uint8_t chord_root;
        chord_type_t chord_type;
        bool active;
        bool bar_chord_played;  /* flag to play chord once per bar when sequencer running */
    } held_chord;

    /* Bar chord notes tracking (for legato playback across the entire bar) */
    struct {
        uint8_t note;
        bool active;
    } bar_chord_notes[12];  /* max 12 notes in a chord */
    int bar_chord_note_count;

    /* Chord step sequencer (Circle mode, rows 5-8 → 32 quarter-note steps,
     * laid out the same as the drum grid: row 8 = steps 0-7, row 7 = 8-15,
     * row 6 = 16-23, row 5 = 24-31.  Loops every 4 drum-pattern cycles.) */
    struct {
        uint8_t    chord_root;    /* absolute semitone 0-11 */
        chord_type_t chord_type;
        bool       active;        /* step is programmed */
    } chord_seq_steps[32];
    int chord_seq_step;           /* 0-31 = currently playing quarter step; -1 when stopped */

    /* Last chord manually selected via rows 1-4 (used to populate seq steps) */
    uint8_t      selected_chord_root;
    chord_type_t selected_chord_type;
    bool         has_selected_chord;

    /* Page state */
    uint8_t page;

    /* Mode button hold tracking (for SEQ_MODE_BOTH) */
    bool drums_btn_held;
    bool keys_btn_held;

    /* Drum fill state (intro / outro) */
    int  fill_steps_remaining; /* > 0 while a fill is playing */
    bool fill_is_outro;        /* true = outro: stop after fill */
    bool fill_outro_done;      /* set by timer; cleared by main-task stop */
    bool fill_outro_pending;   /* wait for next bar boundary, then arm outro */
    bool started_with_fill;    /* this run was started via long-press intro */

    /* Tight voicing (Circle mode) */
    bool    tight_voicing;           /* true = close-position (default) */
    uint8_t tight_prev_notes[12];    /* MIDI notes of last voiced chord */
    int     tight_prev_count;        /* count of notes in tight_prev_notes */

    /* Thread safety */
    portMUX_TYPE mux;
} seq_state_t;

static seq_state_t s_seq;

/* Live "hold while pressed" state for the lower half of Circle mode (rows 5-8).
 * We track the exact MIDI notes that were turned on so release sends precise note-offs. */
static struct {
    uint8_t notes[12];
    int     note_count;
} s_circle_lower_hold = {};

/* Per-raw-note remap table for external MIDI in FIELD mode.
 * Stores the quantized note that was actually started for each incoming note,
 * so note-offs are always sent to the correct playing voice even if the
 * harmonic state changes between press and release. */
static uint8_t s_field_note_map[128] = {};

/* Returns true if `note` is currently pressed by the user (Launchpad FIELD pad
 * or external MIDI keyboard).  Any MIDI note-off guarded by this check will
 * be skipped so that automatic chord transitions never cut a user-held note.
 * In non-FIELD mode the remap table stores raw→raw for keyboard notes, so
 * keyboard notes remain visible regardless of the current sequencer mode. */
static bool is_user_held(uint8_t note)
{
    /* Launchpad FIELD pad notes */
    for (int i = 0; i < 64; i++) {
        if (s_seq.field_notes[i].active && s_seq.field_notes[i].note == note)
            return true;
    }
    /* External MIDI keyboard notes (values in the remap table) */
    for (int i = 0; i < 128; i++) {
        if (s_field_note_map[i] != 0 && s_field_note_map[i] == note)
            return true;
    }
    return false;
}

/* ================================================
 * Timer helpers
 * ================================================ */

static uint64_t step_period_us(uint16_t bpm)
{
    /* 16th note period = 60,000,000 / (bpm * 4) microseconds */
    return 60000000ULL / ((uint64_t)bpm * 4);
}

/* ================================================
 * MIDI routing: drums → TSF when loaded, else Dexed
 * ================================================ */

/* Channel marker: active_notes[].channel == 9 means the note was sent to TSF. */
#define SEQ_CH_TSF  9
#define SEQ_CH_DEX  0

static inline void seq_drum_note_on(uint8_t note, uint8_t vel)
{
    if (tsf_engine_is_loaded()) {
        tsf_engine_handle_midi(0x99, note, vel);  /* channel 9 */
    } else {
        dexed_raw_handle_midi(0x90, note, vel);
    }
}

static inline void seq_drum_note_off(uint8_t note)
{
    if (tsf_engine_is_loaded()) {
        tsf_engine_handle_midi(0x89, note, 0);
    } else {
        dexed_raw_handle_midi(0x80, note, 0);
    }
}

/* Send Note Off to the correct engine based on stored channel marker. */
static inline void seq_note_off_routed(uint8_t channel, uint8_t note)
{
    if (channel == SEQ_CH_TSF) {
        tsf_engine_handle_midi(0x89, note, 0);
    } else {
        dexed_raw_handle_midi((uint8_t)(0x80 | channel), note, 0);
    }
}

/* ================================================
 * Timer callback -- called from esp_timer task (Core 0)
 * ================================================ */

/* Long-press tracking for SESSION button (main-task read only outside ISR) */
static volatile int64_t s_session_press_us = 0;
#define SESSION_LONG_PRESS_US  500000LL   /* 500 ms */

/* ---- Drum fill patterns (16 steps each) ----
 * Each row: drum-track index, intro velocities, outro velocities.
 * Intro: builds up into the groove (crash on beat 1, kick on quarters, snare
 *        roll at end).  Outro: snare roll building to crash on final step. */
#define FILL_PATTERN_LEN 16
struct fill_entry { int idx; uint8_t in_v[16]; uint8_t out_v[16]; };
static const fill_entry s_fill_patterns[] = {
    { 0,  /* kick */
      {100,0,0,0,100,0,0,0,100,0,0,0,100,0,0,  0},
      {100,0,0,0,  0,0,0,0,100,0,0,0,  0,0,0,  0} },
    { 2,  /* snare */
      {  0,0,0,0, 80,0,0,0,  0,0,0,0, 90,90,100,100},
      {  0,0,80,70,80,70,80,70,  0,70,80,90,100,100,100,  0} },
    { 6,  /* closed hi-hat */
      { 60,60,60,60, 60,60,60,60, 80,80,80,80,  0, 0, 0,  0},
      { 70,70,70,70, 70,70,70,70, 80,80,80,90,  0, 0, 0,  0} },
    { 13, /* crash cymbal (GM note 49) */
      { 90, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0,  0},
      {  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0,100} },
};
#define FILL_NUM_TRACKS 4

/* ---------------------------------------------------------------------------
 * voice_tight_notes — close-position chord voicing
 *
 * Given a chord mask (bits 0-11 = semitones above root) and the absolute
 * root pitch class, positions each note as close as possible to the centroid
 * of the previous chord (default A3=57 when no prev).  Output notes are in
 * MIDI range 24-108.
 * -------------------------------------------------------------------------*/
static void voice_tight_notes(uint16_t mask, uint8_t abs_root,
                               const uint8_t *prev, int prev_cnt,
                               uint8_t *out, int *out_cnt)
{
    uint8_t classes[12]; int n = 0;
    for (int i = 0; i < 12; i++)
        if ((mask >> i) & 1) classes[n++] = (uint8_t)((abs_root + i) % 12);
    int center = 57;  /* A3 default */
    if (prev_cnt > 0) {
        int sum = 0;
        for (int i = 0; i < prev_cnt; i++) sum += prev[i];
        center = sum / prev_cnt;
    }
    int cnt = 0;
    for (int i = 0; i < n; i++) {
        int pc = (int)classes[i];
        int note = pc + ((center - pc + 6) / 12) * 12;
        while (note < 24)  note += 12;
        while (note > 108) note -= 12;
        out[cnt++] = (uint8_t)note;
    }
    *out_cnt = cnt;
}

static void seq_timer_callback(void *arg)
{
    seq_state_t *s = (seq_state_t *)arg;

    portENTER_CRITICAL(&s->mux);

    /* Guard: outro fill has finished — wait for main task to call stop */
    if (s->fill_outro_done) {
        portEXIT_CRITICAL(&s->mux);
        return;
    }

    /* 1. Note Off for all currently sounding step notes (NOT bar chord notes) */
    for (int i = 0; i < s->active_note_count; i++) {
        seq_note_off_routed(s->active_notes[i].channel, s->active_notes[i].note);
    }
    s->active_note_count = 0;

    /* 2. Advance playhead */
    s->current_step = (s->current_step + 1) % s->num_steps;
    int step = s->current_step;

    /* Arm pending outro at the start of the next bar */
    if (s->fill_outro_pending && step == 0 && s->fill_steps_remaining == 0) {
        s->fill_outro_pending   = false;
        s->fill_is_outro        = true;
        s->fill_outro_done      = false;
        s->fill_steps_remaining = s->num_steps;
        /* Silence all sounding notes immediately so only the fill plays */
        for (int i = 0; i < s->bar_chord_note_count; i++) {
            if (s->bar_chord_notes[i].active) {
                dexed_raw_handle_midi(0x80, s->bar_chord_notes[i].note, 0);
                s->bar_chord_notes[i].active = false;
            }
        }
        s->bar_chord_note_count = 0;
        for (int i = 0; i < s->active_note_count; i++)
            seq_note_off_routed(s->active_notes[i].channel, s->active_notes[i].note);
        s->active_note_count = 0;
    }

    /* 3. At each quarter-note boundary (every 4 steps): advance chord sequencer or bar chord.
     * Skipped during outro fill so no new notes are triggered. */
    if (step % 4 == 0 && s->playing && !s->fill_is_outro) {
        /* Advance chord-seq playhead independently (0-31 over 4 drum loops) */
        s->chord_seq_step = (s->chord_seq_step < 0) ? 0 : (s->chord_seq_step + 1) % 32;
        int q = s->chord_seq_step;

        /* Check whether the chord step sequencer has any programmed steps */
        bool chord_seq_has_steps = false;
        for (int i = 0; i < 32; i++) {
            if (s->chord_seq_steps[i].active) { chord_seq_has_steps = true; break; }
        }

        uint8_t play_root = 0xFF;   /* 0xFF = no chord this quarter */
        chord_type_t play_type = CHORD_MAJ;

        if (chord_seq_has_steps) {
            if (s->chord_seq_steps[q].active) {
                play_root = s->chord_seq_steps[q].chord_root;
                play_type = s->chord_seq_steps[q].chord_type;
            }
        } else if (step == 0 && s->held_chord.active) {
            /* Legacy: held chord fires once per 2-bar loop (only when no chord seq programmed) */
            play_root = s->held_chord.chord_root;
            play_type = s->held_chord.chord_type;
        }

        /* At every quarter boundary that belongs to the chord sequencer, stop the previous
         * chord ONLY when a new chord is about to play — this gives legato sustain across
         * empty steps so chords ring until the next programmed step. */
        if (!chord_seq_has_steps && step == 0 && s->held_chord.active) {
            /* Legacy: held chord fires once per 2-bar loop */
            play_root = s->held_chord.chord_root;
            play_type = s->held_chord.chord_type;
        }

        if (play_root != 0xFF) {
            /* Stop previous bar chord notes now that a new one is starting */
            for (int i = 0; i < s->bar_chord_note_count; i++) {
                if (s->bar_chord_notes[i].active) {
                    if (!is_user_held(s->bar_chord_notes[i].note))
                        dexed_raw_handle_midi(0x80, s->bar_chord_notes[i].note, 0);
                    s->bar_chord_notes[i].active = false;
                }
            }
            s->bar_chord_note_count = 0;
            /* Update harmonic state so FIELD page + suggestion engine reflect the new chord */
            uint8_t abs_old = (s->harmonic.key + s->harmonic.chord_root) % 12;
            s->harmonic.prev_chord_root = abs_old;
            s->harmonic.prev_chord_type = s->harmonic.chord_type;
            s->harmonic.has_prev_chord  = true;
            s->harmonic.chord_root = (play_root - s->harmonic.key + 12) % 12;
            s->harmonic.chord_type = play_type;

            /* Keep held_chord in sync so the Circle display stays coherent */
            if (chord_seq_has_steps) {
                s->held_chord.chord_root = play_root;
                s->held_chord.chord_type = play_type;
                s->held_chord.active     = true;
            }

            /* Play new chord notes (legato — sustains until next chord trigger) */
            uint16_t mask = chord_masks[play_type];
            if (s->tight_voicing) {
                uint8_t tnotes[12]; int tcnt = 0;
                voice_tight_notes(mask, play_root,
                                  s->tight_prev_notes, s->tight_prev_count,
                                  tnotes, &tcnt);
                for (int i = 0; i < tcnt && s->bar_chord_note_count < 12; i++) {
                    uint8_t note = tnotes[i];
                    if (note <= 127) {
                        dexed_raw_handle_midi(0x90, note, 64);
                        s->bar_chord_notes[s->bar_chord_note_count].note   = note;
                        s->bar_chord_notes[s->bar_chord_note_count].active = true;
                        s->bar_chord_note_count++;
                    }
                }
            } else {
                uint8_t base_note = (uint8_t)((s->base_octave + 3) * 12 + play_root);
                for (int i = 0; i < 12 && s->bar_chord_note_count < 12; i++) {
                    if ((mask >> i) & 1) {
                        uint8_t note = base_note + i;
                        if (note <= 127) {
                            dexed_raw_handle_midi(0x90, note, 64);   /* 50% velocity */
                            s->bar_chord_notes[s->bar_chord_note_count].note   = note;
                            s->bar_chord_notes[s->bar_chord_note_count].active = true;
                            s->bar_chord_note_count++;
                        }
                    }
                }
            }
            /* Update tight voicing centroid */
            s->tight_prev_count = s->bar_chord_note_count;
            for (int i = 0; i < s->bar_chord_note_count; i++)
                s->tight_prev_notes[i] = s->bar_chord_notes[i].note;
        }
    }

    /* 4. Trigger notes at current step.
     * Playback is always active regardless of display mode — mode only controls
     * which page is shown on the Launchpad, not which tracks fire. */

    /* Fill intercept: play fill patterns instead of programmed tracks */
    if (s->fill_steps_remaining > 0) {
        int fi_step = step % FILL_PATTERN_LEN;
        for (int fi = 0; fi < FILL_NUM_TRACKS; fi++) {
            uint8_t vel = s->fill_is_outro
                        ? s_fill_patterns[fi].out_v[fi_step]
                        : s_fill_patterns[fi].in_v[fi_step];
            if (vel > 0)
                seq_drum_note_on(drum_midi_notes[s_fill_patterns[fi].idx], vel);
        }
        s->fill_steps_remaining--;
        if (s->fill_steps_remaining == 0) {
            if (s->fill_is_outro) {
                /* Stop all notes and signal main task to call step_seq_stop() */
                for (int i = 0; i < s->active_note_count; i++)
                    seq_note_off_routed(s->active_notes[i].channel,
                                        s->active_notes[i].note);
                s->active_note_count = 0;
                for (int i = 0; i < s->bar_chord_note_count; i++) {
                    if (s->bar_chord_notes[i].active) {
                        dexed_raw_handle_midi(0x80, s->bar_chord_notes[i].note, 0);
                        s->bar_chord_notes[i].active = false;
                    }
                }
                s->bar_chord_note_count = 0;
                /* Signal main task to call step_seq_stop() — do NOT set playing=false
                 * here; step_seq_stop() must do it so the timer gets cancelled. */
                s->fill_outro_done = true;
            }
            /* Intro fill done: normal playback resumes from next tick */
        }
        portEXIT_CRITICAL(&s->mux);
        return;
    }

    for (int d = 0; d < SEQ_NUM_DRUM_TRACKS; d++) {
        uint8_t vel = s->drum_tracks[d].steps[step].velocity;
        if (vel > 0) {
            seq_drum_note_on(drum_midi_notes[d], vel);
            /* Drum notes NOT tracked in active_notes[]: sample engine handles
             * its own decay; note-off on next tick would cut samples short. */
        }
    }
    {
        seq_melodic_step_t *ms = &s->melodic_steps[step];
        for (int n = 0; n < ms->note_count && s->active_note_count < SEQ_ACTIVE_NOTES_MAX; n++) {
            if (ms->notes[n] > 0) {
                dexed_raw_handle_midi(0x90, ms->notes[n], ms->velocities[n]);
                s->active_notes[s->active_note_count].note    = ms->notes[n];
                s->active_notes[s->active_note_count].channel = 0;
                s->active_note_count++;
            }
        }
    }

    /* Lookahead for FIELD display: switch the display chord SEQ_FIELD_LOOKAHEAD_MS
     * before it actually plays.  Comparison is in µs so the window is a fixed
     * wall-clock duration, fully independent of BPM. */
    s->display_harmonic = s->harmonic;
    if (s->playing && s->chord_seq_step >= 0) {
        uint64_t period_us          = step_period_us(s->bpm);
        int      steps_to_next      = 4 - (step % 4);          /* 1-4 steps until next quarter */
        uint64_t time_to_next_us    = (uint64_t)steps_to_next * period_us;
        if (time_to_next_us <= (uint64_t)SEQ_FIELD_LOOKAHEAD_MS * 1000ULL) {
            int next_q = (s->chord_seq_step + 1) % 32;
            if (s->chord_seq_steps[next_q].active) {
                s->display_harmonic.chord_root =
                    (s->chord_seq_steps[next_q].chord_root - s->harmonic.key + 12) % 12;
                s->display_harmonic.chord_type = s->chord_seq_steps[next_q].chord_type;
            }
        }
    }

    portEXIT_CRITICAL(&s->mux);

    /* 5. Update Launchpad display (outside critical section) */
    if (launchpad_is_connected()) {
        launchpad_refresh_grid();
    }
}

/* ================================================
 * Public API: init / deinit
 * ================================================ */

int step_seq_init(void)
{
    memset(&s_seq, 0, sizeof(s_seq));
    s_seq.mux = portMUX_INITIALIZER_UNLOCKED;
    s_seq.bpm = SEQ_DEFAULT_BPM;
    s_seq.num_steps = SEQ_MAX_STEPS;
    s_seq.current_step = -1;
    s_seq.current_velocity = SEQ_DEFAULT_VELOCITY;
    s_seq.base_octave = 3;  /* C3 = MIDI 60 */

    /* Initialize harmonic state */
    s_seq.harmonic.key = 0;         /* C */
    s_seq.harmonic.chord_root = 0;  /* Tonic */
    s_seq.harmonic.chord_type = CHORD_MAJ;
    s_seq.harmonic.scale_type = SCALE_MAJOR;
    s_seq.harmonic.tension = 0;
    s_seq.harmonic.prev_chord_root = 0;
    s_seq.harmonic.prev_chord_type = CHORD_MAJ;
    s_seq.harmonic.has_prev_chord = false;
    s_seq.display_harmonic = s_seq.harmonic; /* starts identical */

    /* Initialize held chord tracking */
    s_seq.held_chord.chord_root = 0;
    s_seq.held_chord.chord_type = CHORD_MAJ;
    s_seq.held_chord.active = false;
    s_seq.held_chord.bar_chord_played = false;

    /* Tight voicing defaults */
    s_seq.tight_voicing    = true;
    s_seq.tight_prev_count = 0;

    /* Initialize field notes tracking */
    for (int i = 0; i < 64; i++) {
        s_seq.field_notes[i].note = 0;
        s_seq.field_notes[i].active = false;
    }

    /* Initialize chord step sequencer */
    for (int i = 0; i < 32; i++) {
        s_seq.chord_seq_steps[i].chord_root = 0;
        s_seq.chord_seq_steps[i].chord_type = CHORD_MAJ;
        s_seq.chord_seq_steps[i].active = false;
    }
    s_seq.chord_seq_step = -1;
    s_seq.selected_chord_root = 0;
    s_seq.selected_chord_type = CHORD_MAJ;
    s_seq.has_selected_chord  = false;

    esp_timer_create_args_t timer_args = {
        .callback = seq_timer_callback,
        .arg = &s_seq,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "step_seq",
        .skip_unhandled_events = true,
    };

    esp_err_t err = esp_timer_create(&timer_args, &s_seq.timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create sequencer timer: %s", esp_err_to_name(err));
        return -1;
    }

    launchpad_init();

    ESP_LOGI(TAG, "Step sequencer initialized (BPM=%d, steps=%d)",
             s_seq.bpm, s_seq.num_steps);
    return 0;
}

void step_seq_deinit(void)
{
    step_seq_stop();
    if (s_seq.timer) {
        esp_timer_delete(s_seq.timer);
        s_seq.timer = NULL;
    }
}

/* ================================================
 * Transport
 * ================================================ */

void step_seq_play(void)
{
    if (s_seq.playing) return;
    s_seq.playing = true;
    s_seq.current_step = -1;  /* will advance to 0 on first tick */
    uint64_t period = step_period_us(s_seq.bpm);
    esp_timer_start_periodic(s_seq.timer, period);
    ESP_LOGI(TAG, "Sequencer started (BPM=%d, period=%llu us)",
             s_seq.bpm, (unsigned long long)period);
    esp32_oled_show_transport(true);

    if (launchpad_is_connected()) {
        launchpad_refresh_grid();
    }
}

void step_seq_stop(void)
{
    if (!s_seq.playing) return;
    esp_timer_stop(s_seq.timer);
    s_seq.playing          = false;
    s_seq.started_with_fill = false;
    s_seq.fill_outro_pending = false;

    /* All notes off */
    portENTER_CRITICAL(&s_seq.mux);
    for (int i = 0; i < s_seq.active_note_count; i++) {
        seq_note_off_routed(s_seq.active_notes[i].channel, s_seq.active_notes[i].note);
    }
    s_seq.active_note_count = 0;
    
    /* Turn off bar chord notes */
    for (int i = 0; i < s_seq.bar_chord_note_count; i++) {
        if (s_seq.bar_chord_notes[i].active) {
            dexed_raw_handle_midi(0x80, s_seq.bar_chord_notes[i].note, 0);
            s_seq.bar_chord_notes[i].active = false;
        }
    }
    s_seq.bar_chord_note_count = 0;
    
    s_seq.current_step = -1;
    s_seq.chord_seq_step = -1;
    portEXIT_CRITICAL(&s_seq.mux);

    ESP_LOGI(TAG, "Sequencer stopped");
    esp32_oled_show_transport(false);

    if (launchpad_is_connected()) {
        launchpad_refresh_grid();
    }
}

void step_seq_toggle_play(void)
{
    if (s_seq.playing) {
        step_seq_stop();
    } else {
        step_seq_play();
    }
}

bool step_seq_is_playing(void)
{
    return s_seq.playing;
}

bool step_seq_consume_outro_fill_done(void)
{
    if (!s_seq.fill_outro_done) return false;
    s_seq.fill_outro_done = false;
    return true;
}

/* ================================================
 * Mode
 * ================================================ */

void step_seq_set_mode(seq_mode_t mode)
{
    s_seq.mode = mode;
    
    static const char* mode_names[] = {
        "DRUM", "MELODIC", "BOTH", "CIRCLE", "FIELD"
    };
    ESP_LOGI(TAG, "Mode: %s", mode_names[mode]);
    esp32_oled_show_mode((int)mode);

    if (launchpad_is_connected()) {
        launchpad_refresh_grid();
    }
}

seq_mode_t step_seq_get_mode(void)
{
    return s_seq.mode;
}

/* ================================================
 * BPM
 * ================================================ */

void step_seq_set_bpm(uint16_t bpm)
{
    if (bpm < SEQ_MIN_BPM) bpm = SEQ_MIN_BPM;
    if (bpm > SEQ_MAX_BPM) bpm = SEQ_MAX_BPM;
    s_seq.bpm = bpm;

    if (s_seq.playing) {
        esp_timer_restart(s_seq.timer, step_period_us(bpm));
    }
    ESP_LOGI(TAG, "BPM: %d", bpm);
    esp32_oled_show_bpm(bpm);
}

void step_seq_adjust_bpm(int16_t delta)
{
    int new_bpm = (int)s_seq.bpm + delta;
    step_seq_set_bpm((uint16_t)(new_bpm < SEQ_MIN_BPM ? SEQ_MIN_BPM :
                                 (new_bpm > SEQ_MAX_BPM ? SEQ_MAX_BPM : new_bpm)));
}

uint16_t step_seq_get_bpm(void)
{
    return s_seq.bpm;
}

/* ================================================
 * Drum mode operations
 * ================================================ */

void step_seq_select_drum(uint8_t drum_index)
{
    if (drum_index >= SEQ_NUM_DRUM_TRACKS) return;
    s_seq.selected_drum = drum_index;
}

uint8_t step_seq_get_selected_drum(void)
{
    return s_seq.selected_drum;
}

void step_seq_drum_toggle_step(uint8_t step)
{
    if (step >= SEQ_MAX_STEPS) return;
    portENTER_CRITICAL(&s_seq.mux);
    uint8_t *vel = &s_seq.drum_tracks[s_seq.selected_drum].steps[step].velocity;
    if (*vel > 0) {
        *vel = 0;
    } else {
        *vel = s_seq.current_velocity;
    }
    portEXIT_CRITICAL(&s_seq.mux);
}

void step_seq_drum_set_step(uint8_t step, uint8_t velocity)
{
    if (step >= SEQ_MAX_STEPS) return;
    portENTER_CRITICAL(&s_seq.mux);
    s_seq.drum_tracks[s_seq.selected_drum].steps[step].velocity = velocity;
    portEXIT_CRITICAL(&s_seq.mux);
}

void step_seq_drum_clear_step(uint8_t step)
{
    step_seq_drum_set_step(step, 0);
}

/* ================================================
 * Velocity
 * ================================================ */

void step_seq_set_velocity(uint8_t velocity)
{
    if (velocity < 1) velocity = 1;
    if (velocity > 127) velocity = 127;
    s_seq.current_velocity = velocity;
    ESP_LOGD(TAG, "Velocity: %d", velocity);
}

uint8_t step_seq_get_velocity(void)
{
    return s_seq.current_velocity;
}

/* ================================================
 * Melodic mode operations
 * ================================================ */

void step_seq_melodic_toggle_note(uint8_t step, uint8_t midi_note, uint8_t velocity)
{
    if (step >= SEQ_MAX_STEPS) return;
    portENTER_CRITICAL(&s_seq.mux);
    seq_melodic_step_t *ms = &s_seq.melodic_steps[step];

    /* Check if note already exists in this step */
    for (int i = 0; i < ms->note_count; i++) {
        if (ms->notes[i] == midi_note) {
            /* Remove it (shift down) */
            for (int j = i; j < ms->note_count - 1; j++) {
                ms->notes[j] = ms->notes[j + 1];
                ms->velocities[j] = ms->velocities[j + 1];
            }
            ms->note_count--;
            portEXIT_CRITICAL(&s_seq.mux);
            return;
        }
    }

    /* Add note if space available */
    if (ms->note_count < SEQ_MAX_NOTES_PER_STEP) {
        ms->notes[ms->note_count] = midi_note;
        ms->velocities[ms->note_count] = velocity;
        ms->note_count++;
    }
    portEXIT_CRITICAL(&s_seq.mux);
}

void step_seq_melodic_clear_step(uint8_t step)
{
    if (step >= SEQ_MAX_STEPS) return;
    portENTER_CRITICAL(&s_seq.mux);
    s_seq.melodic_steps[step].note_count = 0;
    portEXIT_CRITICAL(&s_seq.mux);
}

void step_seq_set_base_octave(uint8_t octave)
{
    if (octave > 8) octave = 8;
    s_seq.base_octave = octave;
    esp32_oled_show_octave(octave);
}

uint8_t step_seq_get_base_octave(void)
{
    return s_seq.base_octave;
}

/* ================================================
 * Page navigation
 * ================================================ */

void step_seq_page_left(void)
{
    if (s_seq.page > 0) s_seq.page--;
}

void step_seq_page_right(void)
{
    s_seq.page++;
}

uint8_t step_seq_get_page(void)
{
    return s_seq.page;
}

/* ================================================
 * Harmonic State API
 * ================================================ */

void step_seq_set_key(uint8_t key)
{
    if (key >= 12) key = 0;
    s_seq.harmonic.key = key;
    ESP_LOGI(TAG, "Key: %s", note_names[key]);
    esp32_oled_show_key(key);
}

uint8_t step_seq_get_key(void)
{
    return s_seq.harmonic.key;
}

void step_seq_set_chord(uint8_t root, chord_type_t type)
{
    if (root >= 12) root = 0;
    if (type >= CHORD_TYPE_COUNT) type = CHORD_MAJ;
    s_seq.harmonic.chord_root = root;
    s_seq.harmonic.chord_type = type;
    
    uint8_t abs_root = (s_seq.harmonic.key + root) % 12;
    char roman[8] = {0};
    step_seq_chord_roman_numeral(root, type, s_seq.harmonic.scale_type, roman, sizeof(roman));
    ESP_LOGI(TAG, "Chord: %s (%s%s)", roman, note_names[abs_root], chord_type_names[type]);
}

uint8_t step_seq_get_chord_root(void)
{
    return s_seq.harmonic.chord_root;
}

chord_type_t step_seq_get_chord_type(void)
{
    return s_seq.harmonic.chord_type;
}

void step_seq_set_scale(scale_type_t scale)
{
    if (scale >= SCALE_TYPE_COUNT) scale = SCALE_MAJOR;
    s_seq.harmonic.scale_type = scale;
    ESP_LOGI(TAG, "Scale type: %d", scale);
    esp32_oled_show_scale((int)scale);
}

scale_type_t step_seq_get_scale(void)
{
    return s_seq.harmonic.scale_type;
}

void step_seq_set_tension(uint8_t tension)
{
    if (tension > 3) tension = 3;
    s_seq.harmonic.tension = tension;
    ESP_LOGI(TAG, "Tension: %d", tension);
    esp32_oled_show_tension(tension);
}

uint8_t step_seq_get_tension(void)
{
    return s_seq.harmonic.tension;
}

const harmonic_state_t* step_seq_get_harmonic_state(void)
{
    return &s_seq.harmonic;
}

/* ================================================
 * Harmonic Mode Helper Functions
 * ================================================ */

/* Get circle of fifths distance between two notes (0-6) */
static int circle_distance(uint8_t note_a, uint8_t note_b)
{
    int pos_a = semitone_to_circle[note_a % 12];
    int pos_b = semitone_to_circle[note_b % 12];
    int dist = abs(pos_a - pos_b);
    return (dist > 6) ? (12 - dist) : dist;
}

/* Check if a pitch class is a chord tone */
static bool is_chord_tone(uint8_t pitch_class, uint8_t chord_root, chord_type_t chord_type)
{
    int relative = ((int)pitch_class - (int)chord_root + 12) % 12;
    return (chord_masks[chord_type] >> relative) & 1;
}

/* Check if a pitch class is in the scale */
static bool is_scale_tone(uint8_t pitch_class, uint8_t key, scale_type_t scale_type)
{
    int relative = ((int)pitch_class - (int)key + 12) % 12;
    return (scale_masks[scale_type] >> relative) & 1;
}

/* ---- Scale degree extraction ----
 * Returns the MIDI note for scale degree `degree` (0-based) at given octave.
 * degree 0 = root, degree 1 = 2nd, ... degree 6 = 7th.
 * Returns the semitone offset from the key root for each degree.
 */
static const uint8_t major_degrees[7]    = {0, 2, 4, 5, 7, 9, 11};
static const uint8_t minor_degrees[7]    = {0, 2, 3, 5, 7, 8, 10};
static const uint8_t dorian_degrees[7]   = {0, 2, 3, 5, 7, 9, 10};
static const uint8_t mixolyd_degrees[7]  = {0, 2, 4, 5, 7, 9, 10};
static const uint8_t phrygian_degrees[7] = {0, 1, 3, 5, 7, 8, 10};
static const uint8_t lydian_degrees[7]   = {0, 2, 4, 6, 7, 9, 11};
static const uint8_t locrian_degrees[7]  = {0, 1, 3, 5, 6, 8, 10};
/* Pentatonic: only 5 notes — pad columns 6-7 with passing tones */
static const uint8_t penta_maj_degrees[7] = {0, 2, 4, 7, 9, 12, 14};
static const uint8_t penta_min_degrees[7] = {0, 3, 5, 7, 10, 12, 15};

static const uint8_t* get_scale_degrees(scale_type_t st)
{
    switch (st) {
        case SCALE_MAJOR:           return major_degrees;
        case SCALE_MINOR:           return minor_degrees;
        case SCALE_DORIAN:          return dorian_degrees;
        case SCALE_MIXOLYDIAN:      return mixolyd_degrees;
        case SCALE_PHRYGIAN:        return phrygian_degrees;
        case SCALE_LYDIAN:          return lydian_degrees;
        case SCALE_LOCRIAN:         return locrian_degrees;
        case SCALE_PENTATONIC_MAJ:  return penta_maj_degrees;
        case SCALE_PENTATONIC_MIN:  return penta_min_degrees;
        default:                    return major_degrees;
    }
}

/* ---- Suggestion engine ----
 * Computes how strongly a target chord should glow after playing a source chord.
 *
 * Uses circle-of-fifths distance + a diatonic transition bonus table.
 * Returns 0-5 (0 = no suggestion, 5 = very strong).
 */

/* Map an absolute semitone (0-11) to the nearest diatonic function index (0-6)
 * relative to the given key. Returns -1 if not diatonic. */
static int semitone_to_diatonic_index(uint8_t semitone, uint8_t key, scale_type_t scale)
{
    const uint8_t* degrees = get_scale_degrees(scale);
    int rel = ((int)semitone - (int)key + 12) % 12;
    for (int i = 0; i < 7; i++) {
        if (degrees[i] == rel) return i;
    }
    return -1;  /* not diatonic */
}

/*
 * Transition weight table for 7 diatonic chord functions (I=0 .. vii=6).
 * Higher value = more common / expected progression.
 * Rows = "from", Columns = "to".
 *
 *          I  ii iii  IV  V  vi vii
 * I       0   3   2   5  5   4   1
 * ii      2   0   1   2  5   1   2
 * iii     1   1   0   3  1   4   1
 * IV      4   2   1   0  5   2   1
 * V       5   1   2   2  0   3   1
 * vi      2   4   1   4  2   0   1
 * vii     5   1   2   1  2   1   0
 */
static const uint8_t diatonic_transition[7][7] = {
    {0, 3, 2, 5, 5, 4, 1},  /* from I   */
    {2, 0, 1, 2, 5, 1, 2},  /* from ii  */
    {1, 1, 0, 3, 1, 4, 1},  /* from iii */
    {4, 2, 1, 0, 5, 2, 1},  /* from IV  */
    {5, 1, 2, 2, 0, 3, 1},  /* from V   */
    {2, 4, 1, 4, 2, 0, 1},  /* from vi  */
    {5, 1, 2, 1, 2, 1, 0},  /* from vii */
};

/* Compute suggestion score for a target chord given the previous chord.
 * Returns 0-5. Uses diatonic lookup if both are diatonic, otherwise
 * falls back to circle-of-fifths distance. */
static uint8_t compute_suggestion(uint8_t from_root, uint8_t to_root,
                                  const harmonic_state_t* h)
{
    int from_idx = semitone_to_diatonic_index(from_root, h->key, h->scale_type);
    int to_idx   = semitone_to_diatonic_index(to_root, h->key, h->scale_type);

    if (from_idx >= 0 && to_idx >= 0) {
        return diatonic_transition[from_idx][to_idx];
    }

    /* Fallback: circle distance (1=close → high score, 6=far → low) */
    int dist = circle_distance(from_root, to_root);
    if (dist <= 1) return 4;
    if (dist <= 2) return 3;
    if (dist <= 3) return 2;
    return 1;
}

/* Score a note based on harmonic context (higher = more stable) */
static uint8_t score_note(uint8_t midi_note, const harmonic_state_t* h)
{
    uint8_t pc = midi_note % 12;
    uint8_t abs_chord_root = (h->key + h->chord_root) % 12;

    if (pc == abs_chord_root) {
        return 255;  /* Root - maximum stability */
    }
    if (is_chord_tone(pc, abs_chord_root, h->chord_type)) {
        return 200;  /* Other chord tone */
    }
    if (is_scale_tone(pc, h->key, h->scale_type)) {
        return 120;  /* Scale tone */
    }
    return 40;  /* Chromatic / tension */
}

/* Returns true if a MIDI note is "lit" (visible) in FIELD mode given current harmonic state.
 * Matches exactly the color logic in launchpad.cpp FIELD display. */
static bool is_note_lit(uint8_t midi_note, const harmonic_state_t* h)
{
    uint8_t pc            = midi_note % 12;
    uint8_t abs_chord_root = (h->key + h->chord_root) % 12;
    if (pc == abs_chord_root)                                           return true;
    if (is_chord_tone(pc, abs_chord_root, h->chord_type))               return true;
    if (h->tension >= 1 && is_scale_tone(pc, h->key, h->scale_type))   return true;
    if (h->tension >= 2)                                                return true;
    return false;
}

/* Quantize a MIDI note for FIELD mode: if the note is not lit, walk downward
 * (up to 12 semitones) until we find a lit note and return it. */
static uint8_t field_quantize(uint8_t midi_note)
{
    if (s_seq.mode != SEQ_MODE_FIELD) return midi_note;
    /* Walk down until we hit a lit pitch class */
    for (int delta = 0; delta <= 12; delta++) {
        int candidate = (int)midi_note - delta;
        if (candidate < 0) break;
        if (is_note_lit((uint8_t)candidate, &s_seq.display_harmonic)) return (uint8_t)candidate;
    }
    return midi_note;  /* fallback: nothing found, pass through */
}

/* Get the chord root+type for a position in Circle mode grid.
 *
 * Columns map to circle-of-fifths positions centred on the key.
 * Rows map to chord quality: 1-2=Maj, 3-4=Min, 5-6=Dom7, 7-8=Min7.
 */
static void get_circle_chord(uint8_t row, uint8_t col, uint8_t key,
                             uint8_t* out_root, chord_type_t* out_type)
{
    /* col 1-8 → circle offset -3…+4 from key */
    int circle_offset = (int)col - 4;
    int circle_pos = ((int)semitone_to_circle[key] + circle_offset + 12) % 12;
    *out_root = circle_to_semitone[circle_pos];

    /* One row per chord type; rows 5-8 are unused (dark). */
    if (row == 1) {
        *out_type = CHORD_MAJ;
    } else if (row == 2) {
        *out_type = CHORD_MIN;
    } else if (row == 3) {
        *out_type = CHORD_DOM7;
    } else {
        *out_type = CHORD_MIN7;  /* row 4; rows 5-8 not reachable */
    }
}

/* Generate MIDI notes for a chord */
static void play_chord(uint8_t root, chord_type_t type, uint8_t velocity, uint8_t base_octave)
{
    uint16_t mask = chord_masks[type];
    uint8_t base_note = base_octave * 12 + root;

    for (int i = 0; i < 12; i++) {
        if ((mask >> i) & 1) {
            uint8_t note = base_note + i;
            if (note <= 127) {
                dexed_raw_handle_midi(0x90, note, velocity);
            }
        }
    }
}

/* Stop all chord notes */
static void stop_chord(uint8_t root, chord_type_t type, uint8_t base_octave)
{
    uint16_t mask = chord_masks[type];
    uint8_t base_note = base_octave * 12 + root;

    for (int i = 0; i < 12; i++) {
        if ((mask >> i) & 1) {
            uint8_t note = base_note + i;
            if (note <= 127 && !is_user_held(note)) {
                dexed_raw_handle_midi(0x80, note, 0);
            }
        }
    }
}

/* ---- Chromatic Field mode note mapping (2 rows per octave) ----
 *
 * Layout: 2 rows per octave, 8 columns for semitones.
 *   Odd rows (1,3,5,7):  semitones 0-7  (C, C♯, D, D♯, E, F, F♯, G)
 *   Even rows (2,4,6,8): semitones 4-11 (E, F, F♯, G, G♯, A, A♯, B)
 *
 * This overlaps semitones 4-7 across consecutive rows for visual continuity
 * while showing all 12 notes per octave.
 *
 * Row pairs: (1-2, 3-4, 5-6, 7-8) = octaves (base, base+1, base+2, base+3)
 */
static uint8_t get_field_note(uint8_t row, uint8_t col, const harmonic_state_t* h)
{
    uint8_t base_octave = s_seq.base_octave;
    int octave_index = (row - 1) / 2;  /* which pair of rows (0-3) */
    int octave = base_octave + octave_index;
    bool is_even_row = (row % 2 == 0);
    int base_semitone = is_even_row ? 6 : 0;  /* odd rows: 0-5 (C-F), even rows: 6-11 (F#-B) */
    int semitone_offset = base_semitone + (col - 1);
    
    int note = octave * 12 + h->key + semitone_offset;
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    return (uint8_t)note;
}

/* ================================================
 * State queries for display
 * ================================================ */

int step_seq_get_current_step(void)
{
    return s_seq.current_step;
}

bool step_seq_drum_step_is_active(uint8_t drum, uint8_t step)
{
    if (drum >= SEQ_NUM_DRUM_TRACKS || step >= SEQ_MAX_STEPS) return false;
    return s_seq.drum_tracks[drum].steps[step].velocity > 0;
}

uint8_t step_seq_drum_step_velocity(uint8_t drum, uint8_t step)
{
    if (drum >= SEQ_NUM_DRUM_TRACKS || step >= SEQ_MAX_STEPS) return 0;
    return s_seq.drum_tracks[drum].steps[step].velocity;
}

/* ================================================
 * Grid coordinate helpers (local)
 * ================================================ */

/* Q1/Q2 pad → step index (Drum mode) */
static int grid_to_step_index(uint8_t row, uint8_t col)
{
    if (row < 5 || row > 8 || col < 1 || col > 8) return -1;
    return (8 - row) * 8 + (col - 1);
}

/* Q3 pad → drum index */
static int q3_to_drum_index(uint8_t row, uint8_t col)
{
    if (row < 1 || row > 4 || col < 1 || col > 4) return -1;
    return (row - 1) * 4 + (col - 1);
}

/* Q4 pad → velocity index */
static int q4_to_velocity_index(uint8_t row, uint8_t col)
{
    if (row < 1 || row > 4 || col < 5 || col > 8) return -1;
    return (row - 1) * 4 + (col - 5);
}

/* Velocity levels for Q4 */
static const uint8_t velocity_levels[16] = {
    8, 16, 24, 32, 40, 48, 56, 64,
    72, 80, 88, 96, 104, 112, 120, 127
};

/* ================================================
 * Input handlers (called from launchpad.cpp)
 * ================================================ */

void step_seq_handle_grid_press(uint8_t row, uint8_t col, uint8_t velocity)
{
    if (s_seq.mode == SEQ_MODE_DRUM || s_seq.mode == SEQ_MODE_BOTH) {
        if (lp_is_q1(row, col) || lp_is_q2(row, col)) {
            /* Step grid: toggle step for selected drum */
            int step = grid_to_step_index(row, col);
            if (step >= 0 && step < s_seq.num_steps) {
                step_seq_drum_toggle_step((uint8_t)step);
            }
        }
        else if (lp_is_q3(row, col)) {
            /* Drum pad selection + audition.
             * On pressure-sensitive pads (Launchpad X) adopt the actual played
             * velocity as the new current velocity so the next programmed step
             * uses it too.  A pad reporting exactly 64 is almost certainly a
             * fixed-velocity device — keep the existing velocity in that case. */
            int drum = q3_to_drum_index(row, col);
            if (drum >= 0 && drum < SEQ_NUM_DRUM_TRACKS) {
                step_seq_select_drum((uint8_t)drum);
                uint8_t v = (velocity > 0 && velocity != 64)
                            ? velocity
                            : s_seq.current_velocity;
                if (v != s_seq.current_velocity) step_seq_set_velocity(v);
                /* Audition: trigger the drum sound at played velocity */
                seq_drum_note_on(drum_midi_notes[drum], v);
            }
        }
        else if (lp_is_q4(row, col)) {
            /* Velocity selection grid — fixed preset levels per cell */
            int vel_idx = q4_to_velocity_index(row, col);
            if (vel_idx >= 0 && vel_idx < 16) {
                step_seq_set_velocity(velocity_levels[vel_idx]);
            }
        }
    }
    else if (s_seq.mode == SEQ_MODE_MELODIC) {
        if (lp_is_q1(row, col) || lp_is_q2(row, col)) {
            /* Melodic step grid: columns=time, rows=pitch */
            int step = (col - 1);  /* 0-7 */
            uint8_t pitch = (uint8_t)(s_seq.base_octave * 12 + (row - 5));
            step_seq_melodic_toggle_note((uint8_t)step, pitch, s_seq.current_velocity);
        }
        else if (lp_is_q3(row, col) || lp_is_q4(row, col)) {
            /* Keyboard: play note live */
            uint8_t base = s_seq.base_octave * 12;
            int offset;
            if (col <= 4) {
                offset = (row - 1) * 4 + (col - 1);
            } else {
                offset = 16 + (row - 1) * 4 + (col - 5);
            }
            uint8_t note = (uint8_t)(base + offset);
            if (note <= 127) {
                dexed_raw_handle_midi(0x90, note, velocity);
            }
        }
    }
    else if (s_seq.mode == SEQ_MODE_CIRCLE) {
        /* Circle of Fifths chord mode - rows 1-4 only (rows 5-8 are dark/unused) */
        if (lp_is_grid(row, col) && row <= 4) {
            uint8_t chord_root;
            chord_type_t chord_type;
            get_circle_chord(row, col, s_seq.harmonic.key, &chord_root, &chord_type);

            /* Stop any previously held notes — but never cut a user-held note */
            for (int i = 0; i < s_circle_lower_hold.note_count; i++)
                if (!is_user_held(s_circle_lower_hold.notes[i]))
                    dexed_raw_handle_midi(0x80, s_circle_lower_hold.notes[i], 0);
            s_circle_lower_hold.note_count = 0;
            /* Also clear bar-chord notes fired by the timer */
            portENTER_CRITICAL(&s_seq.mux);
            for (int i = 0; i < s_seq.bar_chord_note_count; i++) {
                if (s_seq.bar_chord_notes[i].active) {
                    if (!is_user_held(s_seq.bar_chord_notes[i].note))
                        dexed_raw_handle_midi(0x80, s_seq.bar_chord_notes[i].note, 0);
                    s_seq.bar_chord_notes[i].active = false;
                }
            }
            s_seq.bar_chord_note_count = 0;
            portEXIT_CRITICAL(&s_seq.mux);

            /* Save previous chord for suggestion engine */
            uint8_t abs_old = (s_seq.harmonic.key + s_seq.harmonic.chord_root) % 12;
            s_seq.harmonic.prev_chord_root = abs_old;
            s_seq.harmonic.prev_chord_type = s_seq.harmonic.chord_type;
            s_seq.harmonic.has_prev_chord = true;

            /* Update harmonic state to new chord */
            s_seq.harmonic.chord_root = (chord_root - s_seq.harmonic.key + 12) % 12;
            s_seq.harmonic.chord_type = chord_type;

            /* Track last manually-selected chord for step assignment */
            s_seq.selected_chord_root = chord_root;
            s_seq.selected_chord_type = chord_type;
            s_seq.has_selected_chord  = true;

            /* Play and record exact notes — stopped precisely on pad release */
            uint16_t mask = chord_masks[chord_type];
            if (s_seq.tight_voicing) {
                uint8_t tnotes[12]; int tcnt = 0;
                voice_tight_notes(mask, chord_root,
                                  s_seq.tight_prev_notes, s_seq.tight_prev_count,
                                  tnotes, &tcnt);
                for (int i = 0; i < tcnt; i++) {
                    uint8_t n = tnotes[i];
                    if (n <= 127 && s_circle_lower_hold.note_count < 12) {
                        dexed_raw_handle_midi(0x90, n, velocity);
                        s_circle_lower_hold.notes[s_circle_lower_hold.note_count++] = n;
                    }
                }
            } else {
                uint8_t base_note = (uint8_t)((s_seq.base_octave + 3) * 12 + chord_root);
                for (int i = 0; i < 12; i++) {
                    if ((mask >> i) & 1) {
                        uint8_t n = base_note + i;
                        if (n <= 127 && s_circle_lower_hold.note_count < 12) {
                            dexed_raw_handle_midi(0x90, n, velocity);
                            s_circle_lower_hold.notes[s_circle_lower_hold.note_count++] = n;
                        }
                    }
                }
            }
            /* Update tight voicing centroid */
            s_seq.tight_prev_count = s_circle_lower_hold.note_count;
            for (int i = 0; i < s_circle_lower_hold.note_count; i++)
                s_seq.tight_prev_notes[i] = s_circle_lower_hold.notes[i];

            ESP_LOGI(TAG, "Circle: %s%s (hold-while-pressed) (prev %s)",
                     note_names[chord_root], chord_type_names[chord_type],
                     s_seq.harmonic.has_prev_chord ? note_names[abs_old] : "none");
        }
        else if (lp_is_grid(row, col) && row >= 5) {
            /* Chord step sequencer: rows 5-8, cols 1-8 → steps 0-31
             * same layout as drum grid: row 8 = steps 0-7, row 7 = 8-15,
             * row 6 = 16-23, row 5 = 24-31 */
            int seq_step = (8 - row) * 8 + (col - 1);  /* 0-31 */
            if (seq_step >= 0 && seq_step < 32 && s_seq.has_selected_chord) {
                bool cleared = false;
                portENTER_CRITICAL(&s_seq.mux);
                if (s_seq.chord_seq_steps[seq_step].active &&
                    s_seq.chord_seq_steps[seq_step].chord_root == s_seq.selected_chord_root &&
                    s_seq.chord_seq_steps[seq_step].chord_type == s_seq.selected_chord_type) {
                    /* Same chord already on this step → clear it */
                    s_seq.chord_seq_steps[seq_step].active = false;
                    cleared = true;
                } else {
                    /* Different or empty → assign last manually-selected chord */
                    s_seq.chord_seq_steps[seq_step].chord_root = s_seq.selected_chord_root;
                    s_seq.chord_seq_steps[seq_step].chord_type = s_seq.selected_chord_type;
                    s_seq.chord_seq_steps[seq_step].active = true;
                }
                portEXIT_CRITICAL(&s_seq.mux);
                if (cleared) {
                    ESP_LOGI(TAG, "ChordSeq step %d cleared", seq_step);
                } else {
                    char _roman[8] = {0};
                    step_seq_chord_roman_numeral(s_seq.selected_chord_root,
                        s_seq.selected_chord_type, s_seq.harmonic.scale_type,
                        _roman, sizeof(_roman));
                    ESP_LOGI(TAG, "ChordSeq step %d: %s (%s%s)", seq_step,
                             _roman,
                             note_names[s_seq.selected_chord_root],
                             chord_type_names[s_seq.selected_chord_type]);
                }
            }
            /* No sound on sequencer grid pads — step-toggle only */
        }
    }
    else if (s_seq.mode == SEQ_MODE_FIELD) {
        /* Melodic field mode - all rows 1-8, cols 1-6 only (cols 7-8 disabled) */
        if (lp_is_grid(row, col) && col <= 6) {
            uint8_t note = get_field_note(row, col, &s_seq.harmonic);
            /* Quantize: if this pad is off (not lit), redirect to nearest lower lit note */
            note = field_quantize(note);

            /* Track the (possibly redirected) note for release */
            int pad_idx = (row - 1) * 8 + (col - 1);
            if (pad_idx >= 0 && pad_idx < 64) {
                s_seq.field_notes[pad_idx].note = note;
                s_seq.field_notes[pad_idx].active = true;
            }
            
            /* Scale velocity by harmonic role:
             *   chord tone (root/other) → full pad velocity
             *   scale tone (yellow)     → 66 %
             *   tension tone (orange)   → 33 % */
            uint8_t score = score_note(note, &s_seq.display_harmonic);
            uint8_t play_vel;
            if (score >= 200) {
                play_vel = velocity;               /* chord tone → full */
            } else if (score >= 120) {
                play_vel = (uint8_t)((velocity * 2 + 2) / 3);   /* ~66 % */
            } else {
                play_vel = (uint8_t)((velocity + 2) / 3);        /* ~33 % */
            }
            if (play_vel < 1) play_vel = 1;

            dexed_raw_handle_midi(0x90, note, play_vel);
            ESP_LOGD(TAG, "Field: note=%d (%s) score=%d vel=%d",
                     note, note_names[note % 12], score, play_vel);
        }
    }

    /* Refresh display */
    if (launchpad_is_connected()) {
        launchpad_refresh_grid();
    }
}

void step_seq_handle_grid_release(uint8_t row, uint8_t col)
{
    /* Send Note Off for auditioned sounds */
    if (s_seq.mode == SEQ_MODE_DRUM || s_seq.mode == SEQ_MODE_BOTH) {
        if (lp_is_q3(row, col)) {
            int drum = q3_to_drum_index(row, col);
            if (drum >= 0 && drum < SEQ_NUM_DRUM_TRACKS) {
                seq_drum_note_off(drum_midi_notes[drum]);
            }
        }
    }
    else if (s_seq.mode == SEQ_MODE_MELODIC) {
        if (lp_is_q3(row, col) || lp_is_q4(row, col)) {
            uint8_t base = s_seq.base_octave * 12;
            int offset;
            if (col <= 4) {
                offset = (row - 1) * 4 + (col - 1);
            } else {
                offset = 16 + (row - 1) * 4 + (col - 5);
            }
            uint8_t note = (uint8_t)(base + offset);
            if (note <= 127) {
                dexed_raw_handle_midi(0x80, note, 0);
            }
        }
    }
    else if (s_seq.mode == SEQ_MODE_CIRCLE) {
        /* Rows 1-4 are hold-while-pressed — stop recorded notes on release */
        if (row <= 4 && s_circle_lower_hold.note_count > 0) {
            for (int i = 0; i < s_circle_lower_hold.note_count; i++)
                dexed_raw_handle_midi(0x80, s_circle_lower_hold.notes[i], 0);
            s_circle_lower_hold.note_count = 0;
        }
        /* Rows 5-8 are step-toggle only — no sound to stop */
    }
    else if (s_seq.mode == SEQ_MODE_FIELD) {
        /* Release note when pad released (cols 7-8 are inactive, no-op) */
        if (lp_is_grid(row, col) && col <= 6) {
            int pad_idx = (row - 1) * 8 + (col - 1);
            if (pad_idx >= 0 && pad_idx < 64 && s_seq.field_notes[pad_idx].active) {
                uint8_t note = s_seq.field_notes[pad_idx].note;
                dexed_raw_handle_midi(0x80, note, 0);
                s_seq.field_notes[pad_idx].active = false;
            }
        }
    }
}

void step_seq_handle_button(uint8_t cc, uint8_t value)
{
    switch (cc) {
        case LP_CC_SESSION:
            if (value > 0) {
                /* Record press time for long-press detection */
                s_session_press_us = esp_timer_get_time();
            } else {
                /* Button released — decide short vs long press */
                if (s_session_press_us == 0) break;  /* spurious release */
                int64_t elapsed = esp_timer_get_time() - s_session_press_us;
                s_session_press_us = 0;
                if (!s_seq.playing) {
                    /* ---- START ---- */
                    if (elapsed >= SESSION_LONG_PRESS_US) {
                        /* Long press: intro fill, mark as fill-start run */
                        portENTER_CRITICAL(&s_seq.mux);
                        s_seq.fill_is_outro        = false;
                        s_seq.fill_outro_done      = false;
                        s_seq.fill_outro_pending   = false;
                        s_seq.fill_steps_remaining = s_seq.num_steps;
                        portEXIT_CRITICAL(&s_seq.mux);
                        step_seq_play();
                        portENTER_CRITICAL(&s_seq.mux);
                        s_seq.started_with_fill = true;
                        portEXIT_CRITICAL(&s_seq.mux);
                        ESP_LOGI(TAG, "Intro fill started");
                        esp32_oled_show_status("FILL", "INTRO");
                    } else {
                        /* Short press: immediate start, no fill */
                        step_seq_play();
                    }
                } else {
                    /* ---- STOP ---- */
                    if (s_seq.started_with_fill || elapsed >= SESSION_LONG_PRESS_US) {
                        /* Outro fill — schedule for next bar boundary */
                        portENTER_CRITICAL(&s_seq.mux);
                        s_seq.fill_outro_pending = true;
                        portEXIT_CRITICAL(&s_seq.mux);
                        ESP_LOGI(TAG, "Outro fill pending (next bar)");
                        esp32_oled_show_status("FILL", "OUTRO");
                    } else {
                        /* Short press on a non-fill-start run: immediate stop */
                        step_seq_stop();
                    }
                }
            }
            break;

        case LP_CC_DRUMS:
            if (value > 0) {
                /* Press DRUMS button */
                if (s_seq.keys_btn_held) {
                    /* KEYS is already held → enter/toggle BOTH */
                    if (s_seq.mode == SEQ_MODE_BOTH) {
                        /* Already in BOTH, exit to MELODIC (KEYS is held) */
                        step_seq_set_mode(SEQ_MODE_MELODIC);
                    } else {
                        /* Enter BOTH */
                        step_seq_set_mode(SEQ_MODE_BOTH);
                    }
                } else if (s_seq.mode == SEQ_MODE_DRUM || s_seq.mode == SEQ_MODE_BOTH) {
                    /* Already in drum mode → cycle through drum presets */
                    int npresets = tsf_engine_get_preset_count();
                    if (npresets > 1) {
                        int next = (tsf_engine_get_current_preset() + 1) % npresets;
                        tsf_engine_select_preset(next);
                        char pname[24] = {0};
                        tsf_engine_copy_preset_name(next, pname, sizeof(pname));
                        esp32_oled_show_status("DRUM KIT", pname[0] ? pname : "?");
                        ESP_LOGI(TAG, "Drum preset: %d '%s'", next, pname[0] ? pname : "?");
                    }
                } else {
                    /* KEYS not held → enter DRUM only */
                    step_seq_set_mode(SEQ_MODE_DRUM);
                }
            }
            s_seq.drums_btn_held = (value > 0);
            break;

        case LP_CC_KEYS:
            if (value > 0) {
                /* Press KEYS button */
                if (s_seq.drums_btn_held) {
                    /* DRUMS is already held → enter/toggle BOTH */
                    if (s_seq.mode == SEQ_MODE_BOTH) {
                        /* Already in BOTH, exit to DRUM (DRUMS is held) */
                        step_seq_set_mode(SEQ_MODE_DRUM);
                    } else {
                        /* Enter BOTH */
                        step_seq_set_mode(SEQ_MODE_BOTH);
                    }
                } else {
                    /* DRUMS not held → enter MELODIC only */
                    step_seq_set_mode(SEQ_MODE_MELODIC);
                }
            }
            s_seq.keys_btn_held = (value > 0);
            break;

        case LP_CC_UP:
            if (value > 0) step_seq_adjust_bpm(+5);
            break;

        case LP_CC_DOWN:
            if (value > 0) step_seq_adjust_bpm(-5);
            break;

        case LP_CC_LEFT:
            if (value > 0) step_seq_page_left();
            break;

        case LP_CC_RIGHT:
            if (value > 0) step_seq_page_right();
            break;

        /* ---- Right column buttons: mode selection ---- */
        case LP_CC_MODE_SEQ:
            if (value > 0) {
                /* Stop any held chord when switching away from Circle mode */
                if (s_seq.held_chord.active) {
                    stop_chord(s_seq.held_chord.chord_root, s_seq.held_chord.chord_type,
                               s_seq.base_octave + 3);
                    s_seq.held_chord.active = false;
                }
                /* Return to step sequencer (Drum mode default) */
                step_seq_set_mode(SEQ_MODE_DRUM);
                ESP_LOGI(TAG, "Mode: Step Sequencer");
            }
            break;

        case LP_CC_MODE_CIRCLE:
            if (value > 0) {
                if (s_seq.mode == SEQ_MODE_CIRCLE) {
                    /* Already in Circle mode → toggle tight voicing */
                    s_seq.tight_voicing = !s_seq.tight_voicing;
                    esp32_oled_show_status("VOICING",
                                          s_seq.tight_voicing ? "TIGHT" : "FREE");
                    ESP_LOGI(TAG, "Tight voicing: %s",
                             s_seq.tight_voicing ? "ON" : "OFF");
                } else {
                    /* Initialize held chord tracking when entering Circle mode */
                    s_seq.held_chord.active = false;
                    step_seq_set_mode(SEQ_MODE_CIRCLE);
                    ESP_LOGI(TAG, "Mode: Circle of Fifths");
                }
            }
            break;

        case LP_CC_MODE_FIELD:
            if (value > 0) {
                /* Do NOT stop the held chord when switching to Field mode.
                 * This allows melody to play "over" the harmony.
                 * The chord sustains until explicitly toggled off or changed.
                 */
                step_seq_set_mode(SEQ_MODE_FIELD);
                ESP_LOGI(TAG, "Mode: Melodic Field (chord sustains)");
            }
            break;

        case LP_CC_KEY_SELECT:
            if (value > 0) {
                /* Cycle through keys: C, C#, D, ... B */
                uint8_t new_key = (s_seq.harmonic.key + 1) % 12;
                step_seq_set_key(new_key);
                ESP_LOGI(TAG, "Key: %s", note_names[new_key]);
            }
            break;

        case LP_CC_SCALE_SELECT:
            if (value > 0) {
                /* Cycle through scale types */
                scale_type_t new_scale = (scale_type_t)((s_seq.harmonic.scale_type + 1) % SCALE_TYPE_COUNT);
                step_seq_set_scale(new_scale);
            }
            break;

        case LP_CC_TENSION:
            if (value > 0) {
                /* Cycle tension 0-3 */
                uint8_t new_tension = (s_seq.harmonic.tension + 1) % 4;
                step_seq_set_tension(new_tension);
            }
            break;

        case LP_CC_CLEAR:
            if (value > 0) {
                /* Stop any held chord first */
                if (s_seq.held_chord.active) {
                    stop_chord(s_seq.held_chord.chord_root, s_seq.held_chord.chord_type,
                               s_seq.base_octave + 3);
                    s_seq.held_chord.active = false;
                }
                /* Turn off bar chord notes */
                portENTER_CRITICAL(&s_seq.mux);
                for (int i = 0; i < s_seq.bar_chord_note_count; i++) {
                    if (s_seq.bar_chord_notes[i].active) {
                        dexed_raw_handle_midi(0x80, s_seq.bar_chord_notes[i].note, 0);
                        s_seq.bar_chord_notes[i].active = false;
                    }
                }
                s_seq.bar_chord_note_count = 0;
                portEXIT_CRITICAL(&s_seq.mux);
                
                /* Reset harmonic state to defaults */
                s_seq.harmonic.key = 0;
                s_seq.harmonic.chord_root = 0;
                s_seq.harmonic.chord_type = CHORD_MAJ;
                s_seq.harmonic.scale_type = SCALE_MAJOR;
                s_seq.harmonic.tension = 0;
                s_seq.harmonic.prev_chord_root = 0;
                s_seq.harmonic.prev_chord_type = CHORD_MAJ;
                s_seq.harmonic.has_prev_chord = false;
                ESP_LOGI(TAG, "Harmonic state reset");
            }
            break;

        default:
            break;
    }

    if (launchpad_is_connected()) {
        launchpad_refresh_grid();
    }
}

/* ================================================
 * Public API wrappers for display / launchpad use
 * ================================================ */

uint8_t step_seq_get_suggestion_score(uint8_t to_root)
{
    if (!s_seq.harmonic.has_prev_chord) {
        /* No previous chord — fall back to relationship to key */
        int dist = circle_distance(to_root, s_seq.harmonic.key);
        if (dist == 0) return 5;
        if (dist <= 1) return 4;
        if (dist <= 2) return 3;
        return 1;
    }
    return compute_suggestion(s_seq.harmonic.prev_chord_root, to_root, &s_seq.harmonic);
}

uint8_t step_seq_field_quantize_note(uint8_t midi_note)
{
    return field_quantize(midi_note);
}

/* Record that `raw` was remapped to `quantized` on note-on.
 * Must be called after every quantized note-on so note-off can find it. */
void step_seq_field_record_noteon(uint8_t raw, uint8_t quantized)
{
    if (raw < 128) s_field_note_map[raw] = quantized ? quantized : raw;
}

/* Return the note to use for note-off corresponding to a raw incoming note,
 * then clear the mapping.  Falls back to `raw` if no mapping was stored.
 * This ensures note-offs are never affected by harmonic changes. */
uint8_t step_seq_field_resolve_noteoff(uint8_t raw)
{
    if (raw >= 128) return raw;
    uint8_t mapped = s_field_note_map[raw];
    s_field_note_map[raw] = 0;
    return mapped ? mapped : raw;
}

const harmonic_state_t* step_seq_get_display_harmonic_state(void)
{
    return &s_seq.display_harmonic;
}

/* Scale velocity by harmonic role when in FIELD mode.
 * Chord tone → full, scale tone (yellow) → ~66 %, tension (orange) → ~33 %.
 * Pass-through unchanged in all other modes. */
uint8_t step_seq_melodic_scale_velocity(uint8_t midi_note, uint8_t velocity)
{
    if (s_seq.mode != SEQ_MODE_FIELD) return velocity;
    uint8_t sc = score_note(midi_note, &s_seq.display_harmonic);
    if (sc >= 200) return velocity;                          /* chord tone */
    if (sc >= 120) return (uint8_t)((velocity * 2 + 2) / 3); /* scale tone ~66 % */
    uint8_t v = (uint8_t)((velocity + 2) / 3);               /* tension ~33 % */
    return v < 1 ? 1 : v;
}

uint8_t step_seq_get_field_note(uint8_t row, uint8_t col)
{
    return get_field_note(row, col, &s_seq.harmonic);
}

/* ================================================
 * Roman numeral helper
 * ================================================ */

/* Per-scale degree offsets from key root (semitones); 0xFF = sentinel */
static const uint8_t s_degree_offsets[SCALE_TYPE_COUNT][8] = {
    {0,2,4,5,7,9,11,0xFF},       /* MAJOR       */
    {0,2,3,5,7,8,10,0xFF},       /* MINOR       */
    {0,2,3,5,7,9,10,0xFF},       /* DORIAN      */
    {0,2,4,5,7,9,10,0xFF},       /* MIXOLYDIAN  */
    {0,1,3,5,7,8,10,0xFF},       /* PHRYGIAN    */
    {0,2,4,6,7,9,11,0xFF},       /* LYDIAN      */
    {0,1,3,5,6,8,10,0xFF},       /* LOCRIAN     */
    {0,2,4,7,9,0xFF,0xFF,0xFF},  /* PENTA_MAJ   */
    {0,3,5,7,10,0xFF,0xFF,0xFF}, /* PENTA_MIN   */
};

static const char * const s_roman_up[7]  = {"I","II","III","IV","V","VI","VII"};
static const char * const s_roman_lo[7]  = {"i","ii","iii","iv","v","vi","vii"};

static bool chord_is_major_quality(chord_type_t t)
{
    return t == CHORD_MAJ || t == CHORD_DOM7 || t == CHORD_AUG
        || t == CHORD_SUS4 || t == CHORD_SUS2;
}

void step_seq_chord_roman_numeral(uint8_t chord_root_rel, chord_type_t type,
                                  scale_type_t scale, char *out, int len)
{
    if (!out || len < 2) return;
    const uint8_t *deg = s_degree_offsets[scale < SCALE_TYPE_COUNT ? (int)scale : 0];
    bool maj = chord_is_major_quality(type);

    /* Exact match */
    for (int i = 0; i < 7 && deg[i] != 0xFF; i++) {
        if (deg[i] == chord_root_rel) {
            snprintf(out, (size_t)len, "%s", maj ? s_roman_up[i] : s_roman_lo[i]);
            return;
        }
    }
    /* Chromatic note — prefix nearest diatonic degree with b or # */
    int best = 0, best_dist = 12;
    for (int i = 0; i < 7 && deg[i] != 0xFF; i++) {
        int d = abs((int)deg[i] - (int)chord_root_rel);
        if (d > 6) d = 12 - d;
        if (d < best_dist) { best_dist = d; best = i; }
    }
    /* flat if chord_root is one semitone below that degree */
    int diff = (int)chord_root_rel - (int)deg[best];
    if (diff < -6) diff += 12;
    if (diff >  6) diff -= 12;
    const char *pfx = (diff < 0) ? "b" : "#";
    snprintf(out, (size_t)len, "%s%s", pfx,
             maj ? s_roman_up[best] : s_roman_lo[best]);
}

bool step_seq_is_chord_tone(uint8_t pitch_class, uint8_t chord_root, chord_type_t type)
{
    return is_chord_tone(pitch_class, chord_root, type);
}

bool step_seq_is_scale_tone(uint8_t pitch_class, uint8_t key, scale_type_t scale)
{
    return is_scale_tone(pitch_class, key, scale);
}

void step_seq_get_circle_chord(uint8_t row, uint8_t col, uint8_t* out_root, chord_type_t* out_type)
{
    get_circle_chord(row, col, s_seq.harmonic.key, out_root, out_type);
}
/* ================================================
 * Chord step sequencer query API
 * ================================================ */

bool step_seq_chord_seq_step_active(uint8_t step)
{
    if (step >= 32) return false;
    return s_seq.chord_seq_steps[step].active;
}

uint8_t step_seq_chord_seq_step_root(uint8_t step)
{
    if (step >= 32) return 0;
    return s_seq.chord_seq_steps[step].chord_root;
}

chord_type_t step_seq_chord_seq_step_type(uint8_t step)
{
    if (step >= 32) return CHORD_MAJ;
    return s_seq.chord_seq_steps[step].chord_type;
}

int step_seq_chord_seq_current(void)
{
    return s_seq.chord_seq_step;
}
