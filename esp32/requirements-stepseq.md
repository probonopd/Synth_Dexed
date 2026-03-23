# Functional specification for Step Sequencer

Launchpad X sequencer that aligns closely with how the **Ableton Push step sequencer actually behaves**—based on real workflow, layouts, and interaction patterns. Architecturally separated from the rest of the firmware (modular). But interacts with the synth engine via internal MIDI routing.

# 1. Core Design Principle (Push-Like Behavior)

Push does NOT treat the grid as static quadrants. Instead:

* The grid is **functionally split into regions**
* The layout changes depending on:

  * Drum vs melodic
  * Step sequencing vs playing
* The most important idea:

  > “Select what to sequence → then place it in time” ([Ableton][1])

---

# 2. Global Grid Partition (Launchpad Adaptation)

We will still use 4 quadrants, but **mapped to Push-style roles**:

```id="push_like_layout"
[ Q1 | Q2 ]
[----+----]
[ Q3 | Q4 ]
```

### Functional Mapping:

| Quadrant          | Role (Push-inspired)                   |
| ----------------- | -------------------------------------- |
| Q1 (top-left)     | Step sequencer (time axis)             |
| Q2 (top-right)    | Extended steps / paging / loop control |
| Q3 (bottom-left)  | Note/drum selection                    |
| Q4 (bottom-right) | Velocity / parameters / modifiers      |

This mirrors Push’s **Loop Selector + 16 Velocity layout split**.

---

# 3. Drum Mode (Push “Loop Selector + 16 Velocities” Hybrid)

## 3.1 Layout

### Q3 (Bottom-left) → **Drum Pads (4×4)**

* Exactly like Push:

  * 16 drum sounds
  * Select one sound to sequence
* Selected pad = active voice

➡️ This matches:

> “The 16 Drum Rack pads are laid out… bottom left” ([Ableton][1])

---

### Q1 + Q2 (Top 4 rows) → **Step Sequencer (32 steps)**

* Each column = step in time
* Each row = time subdivision (or expanded resolution)
* Combined:

  * Q1 = steps 1–16
  * Q2 = steps 17–32

➡️ This extends Push’s typical 16-step view into 32 using both quadrants

Behavior:

* Press pad → add/remove step for selected drum
* Playhead moves left → right

➡️ Matches:

> “Each step sequencer pad corresponds to a 16th note” ([Ableton][1])

---

### Q4 (Bottom-right) → **Velocity / Step Editing**

* 16 pads = velocity levels
* Selecting a step + pressing here sets velocity

➡️ Matches:

> “16 pads represent 16 different velocities” ([Ableton][1])

---

## 3.2 Interaction Model

1. Select drum (Q3)
2. Place steps (Q1/Q2)
3. Adjust velocity (Q4)

This is **exact Push workflow order**.

---

# 4. Instrument Mode (Push “Melodic Sequencer”)

## 4.1 Layout

Push changes paradigm here:

> “Each row = pitch, each column = time” ([Ableton][1])

---

### Q1 + Q2 (Top Half) → **Step Sequencer Grid (Pitch × Time)**

* Columns = time steps
* Rows = pitch

So:

* 8 rows = 8 pitches
* 8 columns = 8 steps (or 16 using Q2)

➡️ Combined:

* Q1 = steps 1–8
* Q2 = steps 9–16

---

### Q3 + Q4 (Bottom Half) → **Note Selection / Scale Keyboard**

Split into:

#### Q3 (Bottom-left):

* Lower octave notes

#### Q4 (Bottom-right):

* Higher octave notes

➡️ Mirrors Push’s:

> “Bottom half used for selecting/playing notes” ([Ableton][1])

---

## 4.2 Interaction Model

Two ways (Push-style):

### A. Direct Step Entry

* Press pad in Q1/Q2 → places note

### B. Select Notes → Assign to Steps

* Select notes in Q3/Q4
* Then press steps

➡️ Matches:

> “Tapping a step adds all selected notes” ([Ableton][1])

---

## 4.3 Polyphony

* Each step can hold multiple notes
* Holding a step reveals its notes in Q3/Q4

---

# 5. Loop & Page Control (Push Loop Selector Concept)

Push uses pads for loop/page selection.

### Implementation:

Use **Top Row Buttons**:

* **Up/Down**

  * Change page (steps 1–16, 17–32, etc.)

* **Left/Right**

  * Move loop window

Additionally:

### Q2 (Top-right quadrant):

* Acts as **page/loop selector**
* Each pad = one page segment

➡️ Matches Push:

> “Loop length pads correspond to pages of steps” ([Ableton][1])

---

# 6. Playhead & Visual Feedback

* Moving column = playhead
* Color states:

| State        | Color       |
| ------------ | ----------- |
| Active step  | Track color |
| Current step | Green       |
| Selected     | White       |
| Muted        | Dim         |

➡️ Matches:

> “Moving green pad acts as playhead” ([Ableton][1])

---

# 7. Right-Side Buttons (Push-Like Behavior Mapping)

Map to Push workflow:

* **Volume / Pan / Sends**

  * Q4 becomes parameter control grid

* **Mute**

  * Hold + press track (Q3 or dedicated track selection)

* **Solo**

  * Same pattern

* **Record Arm**

  * Enables:

    * Live recording
    * Step overdub

---

# 8. Mode Switching

* **Note button**
  → Instrument (Melodic Sequencer)

* **Custom button**
  → Drum Sequencer

* **Session**
  → Stop / return

---

# 9. Critical Behavioral Rules (Push Authenticity)

### 9.1 Selection-Based Sequencing

* You ALWAYS select a sound/note first
* Then place it in time

---

### 9.2 Context-Sensitive Grid

* Same pads = different roles depending on mode

---

### 9.3 Time is Horizontal

* Always left → right progression

---

### 9.4 Pitch is Vertical

* Bottom = low pitch
* Top = high pitch

-----

Here is the **relevant developer / programmer documentation distilled into an implementation-ready spec**, based directly on Novation’s official material for the Novation Launchpad X.

---

# 1. Core Concept: How You Actually Control the Device

To implement your sequencer, everything is built on one key mechanism:

> The device becomes a **fully programmable MIDI grid in Programmer Mode** ([Novation User Guides][1])

In this mode:

* Every pad and button:

  * **sends MIDI when pressed**
  * **lights up when you send MIDI back**
* There is **no internal behavior** (no session mode, no scales, etc.)

➡️ This is exactly what you need for a custom sequencer.

---

# 2. MIDI Interfaces (Important for Architecture)

The device exposes **two separate MIDI ports**:

* **DAW Interface (LPX DAW In/Out)** → used by Ableton (ignore this)
* **MIDI Interface (LPX MIDI In/Out)** → use THIS for your sequencer ([Novation User Guides][2])

➡️ Your software must:

* **read input from LPX MIDI IN**
* **send LED updates to LPX MIDI OUT**

---

# 3. Entering Programmer Mode

You must explicitly switch the device into programmable state.

### SysEx Command:

```
F0 00 20 29 02 0C 0E 01 F7
```

* `01` = Programmer mode
* `00` = Live mode (to exit)

➡️ This disables all default layouts and gives full control ([Novation User Guides][2])

---

# 4. Grid & Control Surface Model

## 4.1 Full Addressable Surface

In Programmer Mode you get:

* **8×8 grid (pads)** → Note messages
* **Top row buttons** → CC messages
* **Right column buttons** → CC messages

➡️ Total: **9×9 logical grid** ([Novation User Guides][3])

---

## 4.2 Input Messages

### Pads (8×8):

* Send: **Note On (0x90–0x9F)**
* Velocity = pressure
* Note Off = Note On with velocity 0 ([Novation User Guides][2])

### Side Buttons (top + right):

* Send: **Control Change (CC)** ([Novation User Guides][3])

---

## 4.3 Output (LED Control)

You control LEDs by sending MIDI back:

### Static color:

* Channel 1:

  * Note On → sets pad color

### Flashing:

* Channel 2

➡️ Color is encoded as velocity value (palette index) ([Novation User Guides][4])

---

# 5. LED Model (Critical for Sequencer UI)

Each pad is **state-less**:

* No internal memory
* You must:

  * Track all states in software
  * Repaint entire grid when needed

### Implication:

Your sequencer must:

* Maintain full UI state
* Send updates for:

  * Step on/off
  * Playhead
  * selection
  * modes

---

# 6. Coordinate Mapping (Conceptual)

Each pad corresponds to a **fixed MIDI note number**.

* 8×8 grid → sequential note numbers
* You must build a mapping:

  * `(x, y) ↔ MIDI note`

➡️ This mapping is static and defined in the programmer reference (not dynamically queried) ([Novation User Guides][3])

---

# 7. Button Mapping (Top + Right)

These send **CC messages**, not notes.

You must:

* Map each CC → logical action:

  * navigation
  * mode switching
  * parameter control

➡️ Important:

* They behave exactly like pads, just different message type

---

# 8. No Built-In Sequencer Features

Unlike Ableton Push, the Launchpad X provides:

* ❌ No step sequencer
* ❌ No scale mode in Programmer mode
* ❌ No velocity layers
* ❌ No note memory

➡️ Even though Note Mode supports scales:

* That system is **NOT accessible in Programmer Mode** ([Novation Support][5])

---

# 9. You Must Implement Everything

Your software must handle:

### Input Layer

* Pad presses → note events
* Button presses → CC events

### Logic Layer

* Sequencer engine
* Timing / clock
* Modes (drum / melodic)
* Scale logic
* Probability / repeat
* Page handling

### Output Layer

* LED updates
* Animation (playhead)

---

# 10. Timing & Playback

There is **no internal clock sync** in Programmer Mode.

You must:

* Run your own timing engine
* Optionally sync to:

  * MIDI clock
  * DAW clock

---

# 11. Mode Switching Strategy (Your Design)

Since hardware modes are disabled:

You implement **virtual modes**, e.g.:

* Drum Mode
* Instrument Mode
* Velocity Edit Mode
* Scale Select Mode

➡️ All via:

* internal state machine
* LED redraws

---

# 12. Performance Considerations

### 12.1 LED Throughput

* Many updates per frame → MIDI bandwidth limit
* Use:

  * batched updates
  * minimal redraws

---

### 12.2 Latency

* Input is immediate
* Output depends on your event loop

---

# 13. Recommended Architecture

## Event Flow:

```
[MIDI IN]
   ↓
[Input Mapper]
   ↓
[State Engine]
   ↓
[Sequencer Engine]
   ↓
[LED Renderer]
   ↓
[MIDI OUT]
```

---

# 14. Key Constraints (Important)

* No hardware feedback unless you send it
* No built-in scale system in programmable mode
* No abstraction layer → raw MIDI only
* Must manage:

  * state
  * rendering
  * interaction


[1]: https://userguides.novationmusic.com/hc/en-gb/articles/23731440793490-Launchpad-X-s-Settings-menu?utm_source=chatgpt.com "Launchpad X's Settings menu – Novation User Guides"
[2]: https://userguides.novationmusic.com/hc/en-gb/articles/24001489325330-MIDI-on-Launchpad-X?utm_source=chatgpt.com "MIDI on Launchpad X – Novation User Guides"
[3]: https://userguides.novationmusic.com/hc/en-gb/articles/23731456402962-Launchpad-X-s-default-MIDI-mappings?utm_source=chatgpt.com "Launchpad X's default MIDI mappings – Novation User Guides"
[4]: https://userguides.novationmusic.com/hc/en-gb/articles/24001475492498-Controlling-the-Launchpad-X-surface?utm_source=chatgpt.com "Controlling the Launchpad X surface – Novation User Guides"
[5]: https://support.novationmusic.com/hc/de/articles/360010307540-Note-Mode-Settings-on-the-Launchpad-X?utm_source=chatgpt.com "Note Mode Settings on the Launchpad X – Novation"
