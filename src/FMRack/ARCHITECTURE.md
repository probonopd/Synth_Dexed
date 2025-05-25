# FMRack Architecture Overview

This document outlines the software architecture of the FMRack application, focusing on its multi-timbral synthesis capabilities.

At its heart, the system is conceived as a digital 'rack' of synthesizer modules, each capable of producing a distinct sound, and all configurable through a unified performance setup. This modularity allows for complex soundscapes, layering, and efficient management of synthesis resources. The core components – the `Rack`, `Performance`, `Module`, and `Dexed` – work in concert to deliver this vision, providing a clear separation of concerns from overall setup and MIDI routing down to the individual sound generation.

## Core Concepts

The system is designed around a hierarchical structure to manage multiple synthesizer instances (modules), each capable of playing a distinct sound (voice) with its own settings.

-   **Performance**: A `Performance` object defines the complete setup for up to 8 parts. This setup includes, for each part, its specific synthesizer voice data (complete DX7 voice as hex bytes), MIDI channel, volume, panning, detune, transpose, note range, controller assignments, reverb send, and other parameters. Such a `Performance` object, encapsulating all these part configurations, is loaded from a single INI file.
-   **Rack**: The `Rack` is the top-level container. It manages a collection of `Module` instances. Its primary responsibilities are to load a multi-part performance configuration (from an INI file via the `Performance` class), instantiate and configure `Module`s based on this data, route incoming MIDI messages to the appropriate `Module`(s), mix the audio output from all active `Module`s, and apply global effects (Compressor and Reverb).
-   **Module**: A `Module` represents a single instrument or sound layer within the `Rack`, corresponding to one part in a `Performance`. Each `Module` is configured using part-specific data from a `Performance` object. It contains one or more `Dexed` instances (up to 4 for unison effects). The `Module` processes MIDI messages assigned to it, manages its `Dexed` instance(s), and applies module-level volume, panning, detune, and reverb send.
-   **Dexed**: This is the core FM synthesizer engine, emulating the Yamaha DX7. It generates stereo audio based on its voice parameters and MIDI input. It handles note on/off, pitch bend, control changes, and SysEx messages to modify its sound. The core functionality is provided by the `Dexed` class from `dexed.h`, outside the `FMRack` namespace, allowing it to be used independently of the `Rack` and `Module` structures.
-   **Compressor**: A dynamics processor effect that controls the dynamic range of the audio signal. The `Compressor` is owned by the `Rack` and processes the final mixed audio output from all `Module`s before it's returned to the host.
-   **Reverb**: A spatial effect that adds ambience and depth to the audio signal. The `Reverb` is owned by the `Rack` and receives audio input from `Module`s via their "Reverb Send" parameter. The reverb output is mixed with the dry signal in the final output.
-   **Unison**: The system supports unison by allowing a `Module` to manage multiple `Dexed` instances (up to 4). Each instance can be configured with its own detune and pan settings, creating a richer sound by layering multiple voices for the same note with slight variations.
-   **MIDI Routing**: The `Rack` routes incoming MIDI messages to the appropriate `Module`(s) based on the MIDI channel. Each `Module` processes these messages and forwards them to its `Dexed` instance(s), which then update their synthesis state accordingly. Messages sent to MIDI channel 16 are typically broadcast to all `Module`s, allowing for global control changes or performance-wide settings.
-   **Platform Independence**: The architecture is designed to be platform-independent, allowing it to run on various operating systems, including Windows, macOS, and Linux. Platform-specific details, such as audio output and MIDI input, are abstracted away to ensure consistent behavior across platforms.

## Architectural Flow

### 1. Initialization & Configuration Loading

1.  The main application creates a `Rack` instance.
2.  The `Rack` is instructed to load a performance setup (e.g., from an INI file).
3.  A loading mechanism (e.g., a method within `Performance` or the `Rack`) parses the INI file. This populates a *single* `Performance` object with the configurations for all enabled parts (up to 8) defined in that INI file.
4.  The `Rack` iterates through the part configurations contained within the loaded `Performance` object. For each enabled part (where `MIDIChannel[N] > 0`), it creates and configures a `Module` instance using the specific settings for that part from the `Performance` object.
5.  Each `Module` uses its part-specific configuration data from the `Performance` object to:
    *   Set module-level parameters (volume, detune, transpose, MIDI channel, pan, reverb send).
    *   Configure controller assignments and portamento settings.
    *   Create and configure its internal `Dexed` instances (1-4 instances based on unison settings).
6.  Each `Dexed` instance:
    *   Initializes its internal synthesis components.
    *   Loads the voice parameters (from the `VoiceData[N]` hex bytes in the `Performance` object).
    *   Applies note range limits and mono/poly mode settings.
    *   Applies voice-specific detune and pan settings (for unison spread).

### 2. MIDI Processing

1.  MIDI messages arrive at the `Rack`.
2.  The `Rack` routes the message to the appropriate `Module`(s) based on MIDI channel (for channel messages).
3.  The target `Module`(s) receive the MIDI message.
4.  The `Module` processes the message (applying transpose, note range filtering) and forwards it to its `Dexed` instance(s).
5.  Each `Dexed` instance processes the MIDI message, which updates its synthesis state.

### 3. Audio Processing

1.  The host application requests a block of audio samples from the `Rack`.
2.  The `Rack` iterates through its active `Module`s, requesting audio from each.
3.  Each `Module` requests audio from its `Dexed` instance(s).
4.  Each `Dexed` instance generates a block of stereo audio samples with its specific detune and pan settings applied.
5.  The `Module` sums the stereo audio from all its `Dexed` instances, then applies module-level volume and panning.
6.  For each `Module`, a portion of its processed audio output is sent to the `Rack`'s `Reverb` effect based on the Module's "Reverb Send" parameter.
7.  The `Rack` sums the dry stereo audio from all its `Module`s.
8.  The `Rack` processes the summed reverb input through its `Reverb` effect and mixes the reverb output with the dry signal.
9.  The combined audio (dry + reverb) is processed through the `Rack`'s `Compressor` (if enabled).
10. The final processed stereo audio is returned to the host.

## Class Diagram (Textual Description)

This section describes the main classes and their relationships.

### Class: `Rack`

*   **Responsibilities**:
    *   Manages a collection of `Module` instances.
    *   Loads multi-part performances from configuration files.
    *   Routes incoming MIDI messages to appropriate `Module`(s).
    *   Owns and manages `Compressor` and `Reverb` effects.
    *   Processes reverb sends from `Module`s and applies effects to the final mix.
    *   Mixes stereo audio output from all `Module`(s) and applies effects processing.
*   **Key Relationships**:
    *   **Aggregates**: `Module` (typically `std::vector<std::unique_ptr<Module>>`). A `Rack` *has* multiple `Module`s.
    *   **Aggregates**: `Compressor` and `Reverb` effects.
    *   **Uses**: `Performance` (for loading configurations and instructing `Module` creation).

### Class: `Module`

*   **Responsibilities**:
    *   Represents a single instrument/sound layer within the `Rack`.
    *   Manages one or more `Dexed` instances (1-4 for unison effects).
    *   Is configured by part-specific data from a `Performance` object.
    *   Processes MIDI messages and disvoicees them to its `Dexed` instance(s).
    *   Applies module-level volume, panning, detune, transpose, and reverb send.
    *   Handles controller assignments and portamento settings.
    *   Manages unison voice allocation and mixing.
*   **Key Relationships**:
    *   **Aggregates**: `Dexed` (typically `std::vector<std::unique_ptr<Dexed>>`). A `Module` *has* one or more `Dexed` instances.
    *   **Uses**: `Performance` (receives its configuration from a `Performance` object).

### Class: `Performance`

*   **Responsibilities**:
    *   Stores all settings for a multi-part sound configuration, defining up to 8 parts. This includes, for each part, its complete DX7 voice data (as 155 hex bytes), MIDI channel, volume, panning, detune, transpose, note ranges, controller assignments, reverb send, etc.
    *   Is populated by parsing an INI file with numbered parameters (e.g., `Volume1`, `Pan2`, etc.).
    *   Provides the configuration data for each part to be used by the `Rack` during `Module` instantiation.
    *   Stores global effects settings (compressor and reverb parameters).
*   **Key Relationships**:
    *   **Contains**: Configuration data for multiple parts, including complete DX7 voice data (155 hex bytes per part), and various scalar properties for each part's configuration.
    *   **Used by**: `Rack` (to load a performance configuration and guide `Module` creation).

### Class: `Dexed`

*   **Responsibilities**:
    *   Core FM synthesis engine (emulates Yamaha DX7) using the `Dexed` class from `dexed.h`, which is outside the `FMRack` namespace.
    *   Generates stereo audio output based on its current voice parameters and MIDI input.
    *   Handles MIDI events (note on/off, CC, pitch bend, SysEx) to modulate its sound.
    *   Manages all internal synthesis parameters (algorithms, operator settings, envelopes, LFO, etc.).
    *   Applies note range limiting and mono/poly mode settings.
*   **Key Relationships**:
    *   Is *contained and used by* `Module`.

### Class: `Compressor`

*   **Responsibilities**:
    *   Provides dynamics processing to control the dynamic range of audio signals.
    *   Processes the final mixed audio output from the `Rack`.
    *   Manages compression parameters (threshold, ratio, attack, release, makeup gain).
*   **Key Relationships**:
    *   Is *owned and used by* `Rack`.

### Class: `Reverb`

*   **Responsibilities**:
    *   Provides spatial reverb effects to add ambience and depth.
    *   Receives audio input from `Module`s via their reverb send parameters.
    *   Manages reverb parameters (size, high/low damping, low pass filter, diffusion, level).
    *   Generates reverb output that is mixed with the dry signal.
*   **Key Relationships**:
    *   Is *owned and used by* `Rack`.
    *   Receives audio input from `Module`s based on their "Reverb Send" settings.

### Summary of Key Relationships:

*   `Rack` --1..*--> `Module` (Aggregation: `Rack` has many `Module`s, one per enabled part, up to 8)
*   `Rack` --1--> `Compressor` (Aggregation: `Rack` owns one `Compressor`)
*   `Rack` --1--> `Reverb` (Aggregation: `Rack` owns one `Reverb`)
*   `Module` --1..*--> `Dexed` (Aggregation: `Module` has one or more `Dexed` instances, up to 4 for unison)

*   `Rack` uses `Performance` (for loading part configurations and creating corresponding `Module`s).
*   `Module` uses part-specific data from `Performance` (to set its parameters and configure its `Dexed` instance).
*   `Module` sends audio to `Rack`'s `Reverb` via "Reverb Send" parameter.

**Simplified ASCII Art Class Diagram:**

```
+-----------------+     uses    +-------------------+
|      Rack       |------------>|    Performance    |
|-----------------|             |-------------------|
| - modules: list |             | - part_configs[]  |
| - compressor    |             | - loadFromFile()  |
| - reverb        |             +-------------------+
| - loadPerf()    |
| - routeMidi()   |
| - mixAudio()    |
+-----------------+
       | 1                |1        |1
       |                  |         |
       | aggregates       |owns     |owns
       | * (0..8)         |         |
+-----------------+      +------------+  +----------+
|     Module      |      | Compressor |  |  Reverb  |
|-----------------|      |------------|  |----------|
| - dexed_engines |      | - threshold|  | - size   |
| - volume        |      | - ratio    |  | - decay  |
| - pan           |      | - attack   |  | - damping|
| - detune        |      | - release  |  | - wetdry |
| - reverb_send   |      +------------+  +----------+
| - processMidi() |              ^             ^
| - getAudio()    |              |             |
+-----------------+    processes final mix   receives
       | 1                       |           reverb sends
       |                         |             |
       | aggregates              |             |
       | * (1..4)                |             |
+-----------------+              |             |
|    FMEngine     |              |             |
|-----------------|              |             |
| - voice_params  |              |             |
| - note_range    |--------------+-------------+
| - mono_mode     |
| - voice_detune  |
| - voice_pan     |
| - generateAudio()|
| - handleMidi()  |
+-----------------+
```

Note that this diagram is a simplified representation of the relationships and does not include all methods or attributes for brevity.

**Comparison to Yamaha TX816 and TX802:**

The architecture of FMRack shares conceptual similarities with Yamaha's classic multi-timbral FM synthesizers, the TX816 and TX802, but with some modern software-centric distinctions.

*   **Yamaha TX816:**
    *   **Analogy:** The TX816 is essentially a rack chassis containing eight independent TF1 modules, each being a complete DX7 synthesizer. This is very similar to the `Rack` in FMRack containing multiple `Module` instances, where each `Module` (with at least one `Dexed` engine) acts like a TF1.
    *   **Multi-timbrality:** Both offer multi-timbrality by having distinct synth voices/modules. The TX816 has 8, FMRack's `Rack` can manage up to 8 `Module`s.
    *   **Configuration:** In the TX816, each TF1 module stores its own voice and settings. While you could set them up for a multi-timbral performance, there isn't a single overarching "Performance" file like FMRack's `Performance` object that defines the entire rack's setup in one go. FMRack's `Performance` object provides a more centralized configuration.
    *   **Unison:** To achieve unison on a TX816, you would manually configure multiple TF1 modules to the same MIDI channel and detune/pan them. FMRack formalizes this by allowing a `Module` to manage multiple `Dexed` instances specifically for unison, which is a more integrated approach.

*   **Yamaha TX802:**
    *   **Analogy:** The TX802 is an 8-part multi-timbral synthesizer with 16-note polyphony shared among the parts. A "Part" in the TX802 is highly analogous to a `Module` in FMRack. The TX802 unit itself is like the `Rack`.
    *   **Performance Management:** The TX802 features "Performance" presets that store the configuration for all 8 parts (voice, MIDI channel, volume, pan, detune, note range, etc.). This is *very* similar to how FMRack's `Performance` object stores the configuration for all its `Module`s, typically loaded from a single INI file.
    *   **Voice Architecture:** Each "Part" in the TX802 draws from a common pool of 16 voices. In FMRack, each `Module` (corresponding to one part) contains one or more complete `Dexed` instances. For unison effects, a `Module` can have up to 4 `Dexed` instances, each with its own detune and pan settings, creating richer sounds than a single voice.
    *   **Flexibility:** FMRack's approach allows both multi-timbral operation (multiple parts on different MIDI channels) and unison effects (multiple `Dexed` instances per part), providing more flexibility than the TX802's shared polyphony model.

In essence, FMRack takes inspiration from the powerful multi-timbral concepts of these hardware legends but implements them with the flexibility and structural clarity afforded by a modern software architecture.

## Performance INI File Format

The `Performance` configuration is stored in an INI file format with numbered parameters for each part. The format is as follows:

```
# TG#
#BankNumber#=0        # 0 .. 127
#VoiceNumber#=1       # 1 .. 32
#MIDIChannel#=1       # 1 .. 16, 0: off, >16: omni mode
#Volume#=100          # 0 .. 127
#Pan#=64              # 0 .. 127
#Detune#=0            # -99 .. 99
#Cutoff#=99           # 0 .. 99
#Resonance#=0         # 0 .. 99
#NoteLimitLow#=0      # 0 .. 127, C-2 .. G8
#NoteLimitHigh#=127   # 0 .. 127, C-2 .. G8
#NoteShift#=0         # -24 .. 24
#ReverbSend#=0        # 0 .. 99
#PitchBendRange#=2    # 0 .. 12
#PitchBendStep#=0     # 0 .. 12
#PortamentoMode#=0    # 0 .. 1
#PortamentoGlissando#=0 # 0 .. 1
#PortamentoTime#=0    # 0 .. 99
#VoiceData#=          # space separated hex numbers of 156 voice parameters. Example: 5F 1D 14 32 63 [....] 20 55
#MonoMode#=0          # 0-off .. 1-On
#ModulationWheelRange#=99 # 0..99
#ModulationWheelTarget#=1 # 0..7
#FootControlRange#=99 # 0..99
#FootControlTarget#=0 # 0..7
#BreathControlRange#=99 # 0..99
#BreathControlTarget#=0 # 0..7
#AftertouchRange#=99  # 0..99
#AftertouchTarget#=0  # 0..7
```

**Parameter Details:**
- `BankNumber[N]` - Bank identifier for voice selection (0..127)
- `VoiceNumber[N]` - Voice/voice number within the bank (1..32)
- `MIDIChannel[N]` - MIDI channel assignment (1..16, 0=off, >16=omni mode)
- `Volume[N]` - Part volume level (0..127)
- `Pan[N]` - Stereo panning (0..127, 64=center)
- `Detune[N]` - Fine tuning in cents (-99..99)
- `Cutoff[N]` - Filter cutoff frequency (0..99)
- `Resonance[N]` - Filter resonance amount (0..99)
- `NoteLimitLow[N]`, `NoteLimitHigh[N]` - Note range limits (0..127, C-2..G8)
- `NoteShift[N]` - Transpose in semitones (-24..24)
- `ReverbSend[N]` - Amount sent to reverb effect (0..99)
- `PitchBendRange[N]` - Pitch bend sensitivity in semitones (0..12)
- `PitchBendStep[N]` - Pitch bend step mode (0..12)
- `PortamentoMode[N]` - Portamento on/off (0/1)
- `PortamentoGlissando[N]` - Glissando mode (0/1)
- `PortamentoTime[N]` - Portamento time (0..99)
- `VoiceData[N]` - Complete DX7 voice data as space-separated hex bytes (156 bytes)
- `MonoMode[N]` - Monophonic mode (0=off, 1=on)

**Controller Assignments per Part:**
- `ModulationWheelRange[N]` (0..99), `ModulationWheelTarget[N]` (0..7)
- `FootControlRange[N]` (0..99), `FootControlTarget[N]` (0..7)
- `BreathControlRange[N]` (0..99), `BreathControlTarget[N]` (0..7)
- `AftertouchRange[N]` (0..99), `AftertouchTarget[N]` (0..7)

**Global Effects Settings:**
- `CompressorEnable` - Enable/disable compressor (0/1)
- `ReverbEnable` - Enable/disable reverb (0/1)
- `ReverbSize` - Reverb room size (0-127)
- `ReverbHighDamp` - High frequency damping (0-127)
- `ReverbLowDamp` - Low frequency damping (0-127)
- `ReverbLowPass` - Low pass filter cutoff (0-127)
- `ReverbDiffusion` - Reverb diffusion amount (0-127)
- `ReverbLevel` - Reverb output level (0-127)

**Key Features:**
- Parts with `MIDIChannel[N]=0` are disabled
- `VoiceData[N]` contains the complete 156-byte DX7 voice as hex values
- Multiple parts can share the same MIDI channel for layering
- Each part can have different detune and pan settings for stereo spread effects
- Controller assignments allow real-time parameter modulation

This format allows complete specification of a multi-timbral setup in a single file, defining how up to 8 parts are configured, routed, and processed within the `Rack`.

## Implementation Specifications

### MIDI Channel Routing
- MIDI messages on channels 1-16 are routed to `Module`s with matching `MIDIChannel[N]` settings
- Multiple `Module`s can share the same MIDI channel for layering effects
- `Module`s with `MIDIChannel[N]=0` are disabled and receive no MIDI messages

### Audio Signal Flow
- Each `Module` processes audio independently: Multiple FMEngines → Sum → Volume → Pan → Reverb Send split
- For unison, each `Dexed` in a `Module` applies its own detune and pan before summing
- Dry signals from all `Module`s are summed before effects processing
- Reverb sends are summed separately and processed through the `Reverb` effect
- Final signal path: (Dry Sum + Reverb Output) → Compressor → Host Output

### Voice Loading
- `VoiceData[N]` contains exactly 156 hex bytes representing a complete DX7 voice
- Voice data is loaded directly into the `Dexed` during initialization
- Bank/Voice numbers in the INI file are for reference only; actual voice data comes from `VoiceData[N]`

### Controller Mapping
- Each part supports 4 controller types: Mod Wheel, Foot Control, Breath Control, Aftertouch
- `*Range[N]` parameters define the amount of modulation (0..99)
- `*Target[N]` parameters define which synthesis parameter is modulated (implementation-specific mapping, 0..7)

### Effects Parameters
- Compressor: Standard dynamics processor with threshold, ratio, attack, release, makeup gain
- Reverb: Algorithmic reverb with room size, frequency-dependent damping, diffusion, and output level
- Effects can be independently enabled/disabled via global settings

## UDP MIDI Input Support

FMRack supports receiving MIDI messages over UDP, in addition to standard hardware MIDI input. This is useful for networked MIDI control, scripting, or integration with other software tools.

- The UDP server is started automatically when the application launches (see `main.cpp`).
- By default, it listens on port 50007 (configurable in the code).
- Incoming UDP packets are expected to contain standard 3-byte MIDI messages, or raw MIDI/SysEx data.
- The UDP server parses each packet and forwards valid MIDI messages to the same processing path as hardware MIDI input, by calling `Rack::processMidiMessage` or `Rack::routeSysexToModules`.
- This means all MIDI routing, channel logic, and module triggering works identically for UDP and hardware MIDI.
- UDP MIDI is especially useful for remote control, headless operation, or bridging from other networked MIDI sources.

**Example UDP usage:**
- Send a 3-byte Note On message to the UDP port, and the corresponding note will play on the appropriate module(s), just as if it was received from a hardware MIDI device.
- SysEx messages can also be sent over UDP and will be routed to the correct modules.

**Security note:**
- The UDP server listens on all interfaces by default. For production or public deployments, consider firewalling or restricting access as needed.