# Harmonic Modes

Meet a new kind of instrument—one that doesn’t ask you to learn music theory, but lets you feel it.

Built around the logic of the Circle of Fifths, this grid-based harmonic instrument transforms an 8×8 pad controller into a living, breathing musical space. Every button is part of a dynamic field where chords pull, melodies flow, and harmony evolves naturally under your fingers.

Play chords without knowing their names. Improvise melodies that always fit. Build progressions that sound intentional—even if you’ve never studied a scale.

Lights don’t just show notes—they show meaning: tension, release, movement, and color. The instrument guides you, responds to you, and subtly nudges your ideas into something musical.

No menus. No theory required.
Just touch, explore, and let harmony happen.

## Design Philosophy

> "We're not building a controller or a sequencer —  
>  we're building a **real-time harmonic intuition engine** where  
>  theory becomes visual, mistakes become interesting, and  
>  beginners sound expressive immediately."

Key principles:
- **Map buttons → relationships**, not fixed notes.
- **Lights encode meaning**: brightness = stability, color = function.
- **Momentum-based suggestions**: after pressing a chord the grid re-lights
  to show "good next moves" without any theory labels.
- **Every pad in Melodic Field always plays a scale note** — always sounds
  good; chord tones just glow brighter.

---

## New Modes

### 1. Circle of Fifths — Chord Mode (`SEQ_MODE_CIRCLE`)

**Purpose**: Build chord progressions intuitively guided by light.

#### Grid Layout (8 × 8)
| Axis     | Meaning |
|----------|---------|
| **Columns 1-8** | Circle-of-fifths positions centred on the current key. Col 4 = tonic root, moving right = dominant direction (+fifths), moving left = subdominant direction (−fifths). |
| **Rows (bottom→top)** | Chord quality, simple → complex. Row 1-2 = Major triad, Row 3-4 = Minor triad, Row 5-6 = Dom7, Row 7-8 = Min7. |

#### Diatonic Chord Awareness
For the current key (e.g. C major), the 7 diatonic chords are:
- I = Cmaj, ii = Dm, iii = Em, IV = Fmaj, V = Gmaj, vi = Am, vii° = Bdim

The grid highlights which pads correspond to diatonic chords
more brightly than non-diatonic ones.

#### Momentum-Based Suggestions (key innovation)
When the user presses a chord pad:
1. Record `prev_chord_root` / `prev_chord_type`.
2. Recompute a **suggestion score** for every other pad using a small
   transition-weight table — common progressions score highest:
   - V → I  (resolution) → strongest glow
   - ii → V → strong glow
   - IV → V → strong
   - vi → IV, IV → I → moderate
   - Distant or unusual chords → dim / subtle
3. Render suggestions as **brightness + color**:
   - Current chord = white (bright)
   - Strong suggestion = cyan (bright)
   - Moderate suggestion = green / yellow
   - Tonic-function pads = blue/green
   - Dominant-function pads = orange/red
   - Subdominant-function pads = yellow
   - Distant chords = dim white

This teaches theory silently: the user follows the lights and
unknowingly builds standard or surprising progressions.

#### Interaction
- Press pad → chord sounds (held while pad held)
- Release pad → chord stops
- Harmonic state updates (key, chord_root, chord_type, prev_chord)
- Grid re-renders with new suggestions

---

### 2. Melodic Field Mode (`SEQ_MODE_FIELD`)

**Purpose**: Play melodies that always fit the current chord/key.

#### Grid Layout (8 × 8) — Scale-Degree Based
| Axis     | Meaning |
|----------|---------|
| **Columns 1–7** | Scale degrees 1–7 of the current scale (e.g. C D E F G A B in C major). |
| **Column 8** | Root of the next octave (same as col 1, one octave up). |
| **Rows 1–8 (bottom→top)** | Octaves, starting at base_octave. Row 1 = lowest, Row 8 = highest. |

**Every pad plays a scale note → always sounds good.**

#### Visual Encoding
| Color | Meaning |
|-------|---------|
| **Blue (bright)** | Root of current chord |
| **Green** | Other chord tones (3rd, 5th, 7th) |
| **Yellow** | Scale tones (safe, not chord tones) |
| **Orange** | Available tensions (when tension ≥ 2) |

When the chord changes (from Circle mode or any source) the
*colors shift* but the *note positions stay consistent* — the grid
"breathes" with harmony instead of jumping.

#### Interaction
- Press pad → note sounds (velocity-sensitive)
- Release pad → note stops
- Chord tones are the brightest → beginner follows the lights
- Scale tones are medium → adds melodic colour
- With higher tension, chromatic passing-tones could light up

---

## Button Mapping

### Right-Column Buttons
| CC | Row | Function |
|----|-----|----------|
| 19 | 1 (bottom) | **Step Sequencer** mode (white = active) |
| 29 | 2 | **Circle of Fifths** mode (blue = active) |
| 39 | 3 | **Melodic Field** mode (green = active) |
| 49 | 4 | **Key select** — cycle C, C#, D … B (color = key) |
| 59 | 5 | **Scale select** — cycle Major, Minor, Dorian… (color = scale) |
| 69 | 6 | **Tension** — cycle 0-3 (green→yellow→orange→red) |
| 79 | 7 | *(reserved)* |
| 89 | 8 (top) | **Clear / Reset** harmonic state (red) |

### Top-Row Buttons (unchanged)
| CC | Button | Function |
|----|--------|----------|
| 91 | UP     | BPM +5 |
| 92 | DOWN   | BPM −5 |
| 93 | LEFT   | Page left / Octave down (in Field) |
| 94 | RIGHT  | Page right / Octave up (in Field) |
| 95 | SESSION| Play / Stop |
| 96 | DRUMS  | Drum mode |
| 97 | KEYS   | Melodic mode |
| 98 | USER   | *(unused)* |

---

## Harmonic State

```c
typedef struct {
    uint8_t key;            // 0-11 (C=0 … B=11)
    uint8_t chord_root;     // 0-11 semitone relative to key
    chord_type_t chord_type;
    scale_type_t scale_type;
    uint8_t tension;        // 0-3
    uint8_t prev_chord_root;   // previous chord root (for suggestions)
    chord_type_t prev_chord_type;
} harmonic_state_t;
```

## Suggestion Engine (lookup table, no AI)

A 7×7 diatonic transition weight table encodes common progressions:

```
         → I   ii  iii  IV   V   vi  vii°
from I      -   3    2   5   5   4    1
from ii     2   -    1   2   5   1    2
from iii    1   1    -   3   1   4    1
from IV     4   2    1   -   5   2    1
from V      5   1    2   2   -   3    1
from vi     2   4    1   4   2   -    1
from vii°  5   1    2   1   2   1    -
```

Values are weights 1-5 that map to LED brightness levels.

## Voice Leading (Field Mode)

Notes stay on the same scale degree positions when chords change.
Only the *colours* shift to reflect new chord tones.
This is the "lights slide slightly" effect from the idea.

## Implementation Files

- `step_sequencer.h` — new enum values, harmonic_state_t additions
- `step_sequencer.cpp` — suggestion engine, scale-degree field layout,
  momentum tracking, diatonic chord awareness
- `launchpad.h` — semantic color constants, right-column CC aliases
- `launchpad.cpp` — rendering for Circle and Field modes, right-column
  button indicators
