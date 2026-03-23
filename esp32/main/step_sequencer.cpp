/*
 * Step Sequencer Engine
 *
 * Push-style step sequencer with drum and melodic modes.
 * Uses esp_timer for high-resolution periodic clock.
 * All note output feeds dexed_raw_handle_midi() via the existing MIDI queue.
 */

#include "step_sequencer.h"
#include "launchpad.h"
#include "dexed_raw.h"
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

    /* Clock */
    uint16_t    bpm;
    esp_timer_handle_t timer;

    /* Active note tracking for Note Off scheduling */
    struct {
        uint8_t note;
        uint8_t channel;
    } active_notes[SEQ_ACTIVE_NOTES_MAX];
    int active_note_count;

    /* Page state */
    uint8_t page;

    /* Thread safety */
    portMUX_TYPE mux;
} seq_state_t;

static seq_state_t s_seq;

/* ================================================
 * Timer helpers
 * ================================================ */

static uint64_t step_period_us(uint16_t bpm)
{
    /* 16th note period = 60,000,000 / (bpm * 4) microseconds */
    return 60000000ULL / ((uint64_t)bpm * 4);
}

/* ================================================
 * Timer callback -- called from esp_timer task (Core 0)
 * ================================================ */

static void seq_timer_callback(void *arg)
{
    seq_state_t *s = (seq_state_t *)arg;

    portENTER_CRITICAL(&s->mux);

    /* 1. Note Off for all currently sounding notes */
    for (int i = 0; i < s->active_note_count; i++) {
        dexed_raw_handle_midi(
            (uint8_t)(0x80 | s->active_notes[i].channel),
            s->active_notes[i].note, 0);
    }
    s->active_note_count = 0;

    /* 2. Advance playhead */
    s->current_step = (s->current_step + 1) % s->num_steps;
    int step = s->current_step;

    /* 3. Trigger notes at current step */
    if (s->mode == SEQ_MODE_DRUM) {
        for (int d = 0; d < SEQ_NUM_DRUM_TRACKS; d++) {
            uint8_t vel = s->drum_tracks[d].steps[step].velocity;
            if (vel > 0 && s->active_note_count < SEQ_ACTIVE_NOTES_MAX) {
                uint8_t note = drum_midi_notes[d];
                dexed_raw_handle_midi(0x90, note, vel);
                s->active_notes[s->active_note_count].note = note;
                s->active_notes[s->active_note_count].channel = 0;
                s->active_note_count++;
            }
        }
    } else {
        seq_melodic_step_t *ms = &s->melodic_steps[step];
        for (int n = 0; n < ms->note_count && s->active_note_count < SEQ_ACTIVE_NOTES_MAX; n++) {
            if (ms->notes[n] > 0) {
                dexed_raw_handle_midi(0x90, ms->notes[n], ms->velocities[n]);
                s->active_notes[s->active_note_count].note = ms->notes[n];
                s->active_notes[s->active_note_count].channel = 0;
                s->active_note_count++;
            }
        }
    }

    portEXIT_CRITICAL(&s->mux);

    /* 4. Update Launchpad display (outside critical section) */
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

    if (launchpad_is_connected()) {
        launchpad_refresh_grid();
    }
}

void step_seq_stop(void)
{
    if (!s_seq.playing) return;
    esp_timer_stop(s_seq.timer);
    s_seq.playing = false;

    /* All notes off */
    portENTER_CRITICAL(&s_seq.mux);
    for (int i = 0; i < s_seq.active_note_count; i++) {
        dexed_raw_handle_midi(
            (uint8_t)(0x80 | s_seq.active_notes[i].channel),
            s_seq.active_notes[i].note, 0);
    }
    s_seq.active_note_count = 0;
    s_seq.current_step = -1;
    portEXIT_CRITICAL(&s_seq.mux);

    ESP_LOGI(TAG, "Sequencer stopped");

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

/* ================================================
 * Mode
 * ================================================ */

void step_seq_set_mode(seq_mode_t mode)
{
    s_seq.mode = mode;
    ESP_LOGI(TAG, "Mode: %s", mode == SEQ_MODE_DRUM ? "DRUM" : "MELODIC");
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
    if (s_seq.mode == SEQ_MODE_DRUM) {
        if (lp_is_q1(row, col) || lp_is_q2(row, col)) {
            /* Step grid: toggle step for selected drum */
            int step = grid_to_step_index(row, col);
            if (step >= 0 && step < s_seq.num_steps) {
                step_seq_drum_toggle_step((uint8_t)step);
            }
        }
        else if (lp_is_q3(row, col)) {
            /* Drum pad selection + audition */
            int drum = q3_to_drum_index(row, col);
            if (drum >= 0 && drum < SEQ_NUM_DRUM_TRACKS) {
                step_seq_select_drum((uint8_t)drum);
                /* Audition: trigger the drum sound */
                dexed_raw_handle_midi(0x90, drum_midi_notes[drum], s_seq.current_velocity);
            }
        }
        else if (lp_is_q4(row, col)) {
            /* Velocity selection */
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

    /* Refresh display */
    if (launchpad_is_connected()) {
        launchpad_refresh_grid();
    }
}

void step_seq_handle_grid_release(uint8_t row, uint8_t col)
{
    /* Send Note Off for auditioned sounds */
    if (s_seq.mode == SEQ_MODE_DRUM) {
        if (lp_is_q3(row, col)) {
            int drum = q3_to_drum_index(row, col);
            if (drum >= 0 && drum < SEQ_NUM_DRUM_TRACKS) {
                dexed_raw_handle_midi(0x80, drum_midi_notes[drum], 0);
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
}

void step_seq_handle_button(uint8_t cc, uint8_t value)
{
    (void)value;

    switch (cc) {
        case LP_CC_SESSION:
            step_seq_toggle_play();
            break;

        case LP_CC_DRUMS:
            step_seq_set_mode(SEQ_MODE_DRUM);
            break;

        case LP_CC_KEYS:
            step_seq_set_mode(SEQ_MODE_MELODIC);
            break;

        case LP_CC_UP:
            step_seq_adjust_bpm(+5);
            break;

        case LP_CC_DOWN:
            step_seq_adjust_bpm(-5);
            break;

        case LP_CC_LEFT:
            step_seq_page_left();
            break;

        case LP_CC_RIGHT:
            step_seq_page_right();
            break;

        default:
            break;
    }

    if (launchpad_is_connected()) {
        launchpad_refresh_grid();
    }
}
