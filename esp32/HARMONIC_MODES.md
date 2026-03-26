# Harmonic Modes User Guide

The Launchpad's harmonic modes turn the 8×8 grid into a musical instrument
guided by music theory. Two new modes sit alongside the existing step-sequencer
pages and are reachable via the **right-column buttons**.

---

## Quick Reference — Right Column Buttons

| Button (CC) | Function | Action |
|-------------|----------|--------|
| Row 1 (19)  | **Sequencer** | Switch back to Drum / Melodic step sequencer |
| Row 2 (29)  | **Circle**    | Enter Circle of Fifths chord mode |
| Row 3 (39)  | **Field**     | Enter Melodic Field mode |
| Row 4 (49)  | **Key**       | Cycle the musical key (C → C♯ → D → … → B) |
| Row 5 (59)  | **Scale**     | Cycle scale type (Major, Minor, Dorian, …) |
| Row 6 (69)  | **Tension**   | Cycle tension level 0–3 |
| Row 7 (79)  | *(reserved)*  | —  |
| Row 8 (89)  | **Clear**     | Reset harmonic state to C Major |

---

## Circle of Fifths Mode

Press the **Circle** button (row 2 right column) to enter.

### Grid Layout

```
         col 1   col 2   col 3   col 4   col 5   col 6   col 7   col 8
         ──────  ──────  ──────  ──────  ──────  ──────  ──────  ──────
         -3 5th  -2 5th  -1 5th  TONIC   +1 5th  +2 5th  +3 5th  +4 5th
rows 7-8  Min7    Min7    Min7    Min7    Min7    Min7    Min7    Min7
rows 5-6  Dom7    Dom7    Dom7    Dom7    Dom7    Dom7    Dom7    Dom7
rows 3-4  Minor   Minor   Minor   Minor   Minor   Minor   Minor   Minor
rows 1-2  Major   Major   Major   Major   Major   Major   Major   Major
```

- **Columns** = positions on the Circle of Fifths, centered on the current key.
  Column 4 is the tonic; moving right goes toward the dominant (V direction);
  moving left goes toward the subdominant (IV direction).
- **Rows** = chord quality, from simple triads at the bottom to 7th chords
  at the top.

### Suggestion Colors

After you play a chord, the grid re-colors to show **where to go next**:

| Color        | Meaning |
|--------------|---------|
| **White**    | Currently playing chord |
| **Blue**     | Tonic of the key (home / resolution target) |
| **Cyan**     | Strongest suggestion (score 5 — e.g. V → I) |
| **Green**    | Good move (score 4) |
| **Yellow/Green** | Moderate suggestion (score 3) |
| **Orange**   | Weak suggestion (score 2) |
| **Dim white** | Distant / unusual move |

The suggestion engine uses a diatonic transition weight table based on common
chord progressions. For example, after playing **V** the tonic **I** lights up
bright cyan. After **ii** the dominant **V** is highlighted.

### Playing

- **Press** a pad to play and **sustain** the chord. The chord plays 3 octaves above
  the normal base octave, allowing you to play melodic notes "over" the harmony.
- **Press the same pad again** to toggle the chord off / stop sustaining.
- **Press a different chord pad** to change to that chord. The previous chord stops
  and the new one starts sustaining.
- **Switch to Field mode** — the chord **continues sustaining**. Play melody notes
  over the held harmony using the chromatic grid.
- **When the sequencer is playing** (SESSION button is green): the selected chord
  plays repeatedly at the start of each bar as full notes, providing continuous
  harmonic backing for your improvisation.

---

## Melodic Field Mode

Press the **Field** button (row 3 right column) to enter.

### Grid Layout — Chromatic 12-Tone (2 Rows per Octave)

```
         col 1   col 2   col 3   col 4   col 5   col 6
         ──────  ──────  ──────  ──────  ──────  ──────
rows 5-6  c       c♯      d       d♯      e       f        ← octave 3 (rows 5-6)
          f♯      g       g♯      a       a♯      b
rows 3-4  c       c♯      d       d♯      e       f        ← octave 2 (rows 3-4)
          f♯      g       g♯      a       a♯      b
rows 1-2  c       c♯      d       d♯      e       f        ← octave 1 (rows 1-2)
          f♯      g       g♯      a       a♯      b
      (rows 7-8: disabled)
```

*(shown with C as the current key; the grid transposes with Key changes)*

- **Rows 1–6 only** are active. Rows 7–8 are disabled (dark, no notes).
- **Odd rows** (1, 3, 5) = semitones **0–5** (C, C♯, D, D♯, E, F)
- **Even rows** (2, 4, 6) = semitones **6–11** (F♯, G, G♯, A, A♯, B)
- **Each pair** (1-2, 3-4, 5-6) represents one octave.

Non-harmonic notes (red) are turned off completely—only chord tones, scale tones, and tension tones light up.

### Note Colors & Tension Control

Colors show how each note relates to the **current chord**. **Tension** controls which notes
are highlighted as "playable":

| Color         | Meaning |
|---------------|---------|
| **Blue**      | Root of the current chord — safest, most prominent |
| **Green**     | Other chord tones (3rd, 5th, 7th of chord) — stable |
| **Yellow**    | Scale tones — consonant passing tones |
| **Orange**    | Chromatic tension notes — color / spice |
| **Dim Red**   | Suppress notes — shown dim to de-emphasize |

#### How Tension Affects Your Improvisation

| Level | What Lights Up | Effect |
|-------|-------------|--------|
| 0     | Blue (root) only | Ultra-conservative — only the chord root shines bright |
| 1     | Blue + Green + Yellow | Standard — chord & scale tones available |
| 2     | Blue + Green + Yellow + Orange | Adventurous — chromatic passing tones light up |
| 3     | All colors equally | Maximum freedom — all notes equally bright |

When you switch chords in Circle mode, all Field colors **instantly update** to reflect the
new harmonic context — the chord tones shift, and visual guidance refreshes in real-time.

---

## Shared Controls

### Key Selection

Press the **Key** button (row 4) to cycle through all 12 keys. The button LED
color changes to indicate the current key. Both Circle and Field modes
transpose to the new key.

### Scale Selection

Press the **Scale** button (row 5) to cycle through available scales:

| Scale | Character |
|-------|-----------|
| Major | Bright, happy |
| Minor | Dark, emotional |
| Dorian | Jazzy minor |
| Mixolydian | Bluesy major |
| Phrygian | Spanish / dark |
| Lydian | Dreamy, bright |
| Locrian | Diminished, tense |
| Pentatonic Major | Simple, folk-like |
| Pentatonic Minor | Bluesy, universal |

### Workflow: Circle → Field (Sustained Harmony)

The recommended workflow combines both modes for real-time improvisation with
sustained harmony:

1. Enter **Circle mode** and **press a chord pad**. The chord sustains at a
   high register (3 octaves above normal).
2. **Switch to Field mode** (the chord keeps playing). Play a melody over the
   sustained harmony using the chromatic grid. Adjust **Tension** to reveal
   more or fewer chromatic options.
3. **Return to Circle mode** (the chord still sustains). Press a new chord pad
   to change the harmony while you improvise.
4. **Toggle off** a chord by pressing it again in Circle mode, or press **Clear**
   to reset everything.
5. Use **Key** and **Scale** buttons anytime to transpose or change the mode.

The high-register chord sits "behind" the melody without competing for the same
note range, creating a natural background harmony layer. This is the essence of
the Harmonic Field Instrument — melody and harmony improvised in parallel.

---

## Integration with Step Sequencer

The harmonic modes exist alongside the original Drum and Melodic step-sequencer
pages. Press the **Sequencer** button (row 1 right column) to return to the
step sequencer at any time. The top-row buttons (BPM, Session play, page
navigation) remain functional in all modes.
