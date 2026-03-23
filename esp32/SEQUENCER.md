# Step Sequencer User Guide

The **Step Sequencer** is a push-style sequencer integrated into your Synth Dexed device. It provides two composition modes—**Drum** and **Melodic**—with real-time control via a connected Novation Launchpad X.

## Overview

### Key Features

- **32-step sequencer** with 16th-note resolution
- **Drum Mode**: 16 General MIDI drum tracks with pattern programming
- **Melodic Mode**: Polyphonic note sequencer with 8-note chords per step
- **Tempo Control**: 20–300 BPM with real-time adjustment
- **Velocity Control**: 16 velocity levels (8–127) for dynamic expression
- **Live Keyboard**: Play notes live while patterns loop in the background
- **Visual Feedback**: Launchpad LEDs show playhead, active steps, and selected controls

---

## Hardware: Novation Launchpad X

The Launchpad X provides a visual 8×8 grid interface. The device is divided into **four quadrants**, each with a specific purpose:

```
   Col 1     Col 5
   |         |
   v         v
Row 1  [ Q3 ] [ Q4 ]    < Drum Selection / Velocity or Keyboard
Row 2  [     ][     ]
Row 3  [     ][     ]
Row 4  [     ][     ]
       -----+-----
Row 5  [ Q1 ] [ Q2 ]    < Step Grid (Drum or Melodic)
Row 6  [     ][     ]
Row 7  [     ][     ]
Row 8  [     ][     ]
       -----+-----
[UP][DOWN][LEFT][RIGHT][PLAY/STOP][DRUMS][KEYS][USER] < Top Row Buttons
```

---

## Transport Controls (Top Row Buttons)

| Button | Function |
|--------|----------|
| **PLAY/STOP** (Session) | Toggle playback on/off. LED shows green (playing) or red (stopped). |
| **DRUMS** | Switch to Drum mode. LED indicates active mode. |
| **KEYS** | Switch to Melodic mode. LED indicates active mode. |
| **UP** | Increase tempo by 5 BPM. Yellow button. |
| **DOWN** | Decrease tempo by 5 BPM. Yellow button. |
| **LEFT** | Navigate to the previous page (future expansion). Sky blue button. |
| **RIGHT** | Navigate to the next page (future expansion). Sky blue button. |

---

## Drum Mode

In **Drum Mode**, you program patterns for 16 drum tracks simultaneously. Each drum is mapped to a unique General MIDI drum note.

### Drum Track Mapping

The 16 drum tracks use General MIDI drum notes:

| # | Note | Sound | # | Note | Sound |
|---|------|-------|---|------|-------|
| 0 | 36 | Kick | 8 | 44 | Pedal HiHat |
| 1 | 37 | Side Stick | 9 | 45 | Low Tom |
| 2 | 38 | Snare | 10 | 46 | Open HiHat |
| 3 | 39 | Clap | 11 | 47 | Lo-Mid Tom |
| 4 | 40 | Elec Snare | 12 | 48 | Hi-Mid Tom |
| 5 | 41 | Lo Floor Tom | 13 | 49 | Crash Cymbal |
| 6 | 42 | Closed HiHat | 14 | 50 | High Tom |
| 7 | 43 | Hi Floor Tom | 15 | 51 | Ride Cymbal |

### Workflow: Programming Drum Patterns

1. **Select a drum** by pressing one of the colored pads in **Q3** (rows 1–4, columns 1–4).
   - The selected drum pad illuminates **white**.
   - The button automatically auditions the drum sound for feedback.
   - Each drum track has a unique color for visual identification.

2. **Set velocity** (optional) by pressing a pad in **Q4** (rows 1–4, columns 5–8).
   - Q4 is divided into 16 pads, from soft (top-left) to loud (bottom-right).
   - The selected velocity lights **white**. Default is approximately medium velocity.
   - Velocity affects the volume and character of each drum hit.

3. **Program the step grid** using **Q1 + Q2** (rows 5–8, columns 1–8).
   - Press a pad to toggle a step on/off for the currently selected drum.
   - Active steps light up in the drum's assigned color.
   - The **playhead** (current playing step) shows as:
     - Bright green if the step is active
     - Dim green if the step is empty
   - Each column represents one time step, each row represents a different playing position.

### Example: Programming a Kick Pattern

1. Press the first pad in Q3 (top-left of the drum selection area) to select the Kick (Drum 0). It lights white.
2. Press **[cols 1, 3, 5]** in **rows 5–8** to create a simple kick pattern across 32 steps.
3. Watch the pattern repeat as the playhead advances during playback.

---

## Melodic Mode

In **Melodic Mode**, you compose pitch-based sequences. Each step can contain up to 8 simultaneous notes, enabling chords and complex polyphonic arrangements.

### Layout

- **Q1 + Q2 (Step Grid)**: Columns represent time steps (32 available), rows represent base pitch.
- **Q3 + Q4 (Keyboard)**: A 4-octave keyboard for live playing. Notes trigger immediately when pressed.

### Workflow: Composing Melodies

1. **Press KEYS** to enter Melodic mode. The KEYS button lights white.

2. **Select a base octave** (default: octave 3, MIDI note 60 = C3).
   - Use the ↑/↓ buttons to adjust the octave range displayed.

3. **Program notes in the step grid** (Q1 + Q2):
   - The grid's rows represent pitches relative to the base octave: row 5 (lowest) to row 8 (highest).
   - Press any pad in the step grid to add or remove a note at that step.
   - A single step can contain up to 8 different notes simultaneously (chords).
   - Active notes light **off** because the grid display is minimal in melodic mode.

4. **Play notes live** using the keyboard in **Q3 + Q4** (rows 1–4, columns 1–8):
   - Colored pads indicate piano keys: white keys are brighter, black keys are blue.
   - The keyboard spans 4 octaves.
   - Notes play immediately when pressed, independent of the sequencer pattern.
   - Release to stop the note.

### Example: Creating a Melodic Phrase

1. Press **KEYS** to enter Melodic mode.
2. In the step grid (Q1 + Q2), press different rows in columns 1–4 to create a simple 4-note phrase.
3. Play along using the keyboard (Q3 + Q4) while the pattern loops.
4. Adjust velocity using Q4 pads (rows 1–4, cols 5–8) if needed.

---

## Velocity Control

Velocity affects the volume and brightness of triggered sounds. 16 velocity levels are available:

| Level | Velocity | Level | Velocity |
|-------|----------|-------|----------|
| 1 | 8 | 9 | 72 |
| 2 | 16 | 10 | 80 |
| 3 | 24 | 11 | 88 |
| 4 | 32 | 12 | 96 |
| 5 | 40 | 13 | 104 |
| 6 | 48 | 14 | 112 |
| 7 | 56 | 15 | 120 |
| 8 | 64 | 16 | 127 |

### Setting Velocity

- In **Drum Mode**: Press a velocity pad in **Q4** (rows 1–4, columns 5–8). The selected velocity lights **white**.
- In **Melodic Mode**: Velocity is fixed at the current setting. Adjust as needed before pressing notes in the step grid.

---

## Tempo & Timing

The sequencer uses **16th-note resolution**, meaning each step represents a 16th note at the current tempo.

### Adjusting Tempo

- Press **UP** to increase tempo by **5 BPM**.
- Press **DOWN** to decrease tempo by **5 BPM**.
- Valid range: **20–300 BPM**.
- Tempo changes apply immediately, even during playback.
- Default tempo: **120 BPM** (≈ 375 ms per 16th note).

### Musical Timing

At 120 BPM:
- 1 step (16th note) = 250 ms
- 4 steps (quarter note) = 1000 ms (1 second)
- 32 steps = 8 seconds per full loop

---

## Display & Visual Feedback

### Launchpad LED Colors

| State | Color | Meaning |
|-------|-------|---------|
| **Playing** | Bright Green | Current playhead position |
| **Playing (with note)** | Bright Green | Playhead at an active step |
| **Drum Active** | Drum's color | Step contains a note for the selected drum |
| **Drum Selected** | White | Currently selected drum in Q3 |
| **Velocity Selected** | White | Currently selected velocity in Q4 |
| **Mode Active** | White | Currently active mode (Drums/Keys button) |
| **Mode Inactive** | Dim White | Inactive mode |
| **Tempo Control** | Yellow | UP/DOWN tempo buttons |
| **Navigation** | Sky Blue | LEFT/RIGHT page buttons |
| **Stopped** | Red | PLAY/STOP button when stopped |
| **Playing** | Green | PLAY/STOP button when playing |
| **Inactive** | Off | Unused pads |

### Real-Time Updates

The display refreshes whenever:
- A step plays (playhead advances)
- You press a pad
- You toggle play/stop
- You change modes
- The Launchpad connects/disconnects

---

## Workflow Tips

### Quick Drum Pattern Creation

1. Press PLAY/STOP to start playback.
2. Select a drum in Q3 (e.g., Kick).
3. While listening, tap pads in Q1/Q2 to program the pattern in real-time.
4. Select the next drum (e.g., Snare) and repeat.
5. Experiment with different velocities to add dynamics.

### Layering Melodies with Live Keyboard

1. Enter Melodic mode (press KEYS).
2. Press PLAY/STOP to start looping the melody.
3. Use the keyboard (Q3 + Q4) to play bass notes or improvise over the loop.
4. Program more notes in the step grid during playback for variation.

### Building Complex Drum Grooves

1. Layer multiple drum tracks: Kick on the beat, Snare offbeat, HiHat faster subdivision.
2. Use different velocities for the same drum to create swing or dynamics.
3. Adjust tempo to test at different speeds.

---

## Technical Notes

### Playback Precision

- The sequencer uses ESP-IDF's hardware timer for sub-millisecond clock accuracy.
- Each step trigger causes all active notes to sound, followed by a Note Off before the next step.
- Notes are sent to the Dexed synthesizer engine via the standard MIDI queue.

### Polyphony (Melodic Mode)

- Each step can contain up to **8 notes** simultaneously.
- The synth polyphony is limited by your Dexed FM engine (typically 16–32 voices).
- Overlapping notes from different steps will use available voices.

### Transport State

- When stopped, the playhead is reset. Pressing PLAY/STOP resumes from step 0.
- Playing patterns while adjusting BPM changes the clock frequency in real-time without stopping playback.

---

## Troubleshooting

### No Sound When Programming

- Ensure PLAY/STOP is actively playing (green LED on PLAY/STOP button).
- Check that the Synth Dexed device volume is set appropriately.
- Verify the Launchpad is connected (launchpad display lights should be on).

### Sequencer Doesn't Start

- Reconnect the Launchpad X via USB.
- Restart the Synth Dexed firmware.
- Check USB connectivity on both ends.

### Notes Continue Sounding / Stuck Notes

- Press PLAY/STOP twice to reset playback.
- All notes are released when playback stops.

### Velocity Changes Don't Apply

- In Drum mode, set velocity first (press Q4 pad), then program steps.
- Existing steps retain their programmed velocity; changing the global velocity setting only affects newly programmed steps.

---

## Quick Reference

| Action | Control |
|--------|---------|
| Start/Stop Playback | PLAY/STOP button |
| Switch to Drum Mode | DRUMS button |
| Switch to Melodic Mode | KEYS button |
| Increase Tempo | UP button (+5 BPM) |
| Decrease Tempo | DOWN button (−5 BPM) |
| Select Drum | Press pad in Q3 |
| Select Velocity | Press pad in Q4 |
| Program Step (Drum) | Press pad in Q1/Q2 |
| Program Note (Melodic) | Press pad in Q1/Q2 |
| Play Note Live (Melodic) | Press pad in Q3/Q4 keyboard |

---

## System Integration

The sequencer integrates seamlessly with your existing Synth Dexed setup:

- **MIDI Output**: All sequencer notes feed the Dexed FM synthesizer via the standard MIDI queue.
- **No Conflict**: Launchpad input is routed exclusively to the sequencer; external MIDI keyboards continue to function normally.
- **Boot Button**: The existing Boot button (Symphonic effect toggle) remains unaffected.

---

## Future Enhancements

Potential features for future versions:

- **Page navigation**: Use LEFT/RIGHT buttons to access steps 32–64, 64–96, etc.
- **Pattern saving/loading**: Store and recall multiple drum and melodic patterns.
- **Time signature/swing**: Adjust the rhythmic feel beyond the fixed 32-step, 16th-note grid.
- **Undo/redo**: Revert recent edits.
