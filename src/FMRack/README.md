# FMRack

This document outlines the software architecture of the FMRack application.

At its heart, the system is conceived as a digital 'rack' of synthesizer modules, each capable of producing a distinct sound, and all configurable through a unified performance setup. This modularity allows for complex soundscapes, layering, and efficient management of synthesis resources. The core components – the `Rack`, `Performance`, `Module`, and `Dexed` – work in concert to deliver this vision, providing a clear separation of concerns from overall setup and MIDI routing down to the individual sound generation.

## Source Code Organization

* `src/FMRack/` - Contains the main application code for the FMRack synthesizer.	
* `src/FMRack/README.md` - Documentation outlining the architecture and usage of the FMRack application. 
* `src/` - Contains `dexed.h`, the header file for the Dexed FM synthesis engine.

## Core Concepts

The system is designed around a hierarchical structure to manage multiple synthesizer instances (modules), each capable of playing a distinct sound (voice) with its own settings.

-   **Performance**: A `Performance` object defines the complete setup for up to 16 parts. This setup includes, for each part, its specific synthesizer voice data (complete DX7 voice as hex bytes), MIDI channel, volume, panning, detune, transpose, note range, controller assignments, reverb send, and other parameters. Such a `Performance` object, encapsulating all these part configurations, is loaded from a single INI file.
-   **Rack**: The `Rack` is the top-level container. It manages a collection of `Module` instances. Its primary responsibilities are to load a multi-part performance configuration (from an INI file via the `Performance` class), instantiate and configure `Module`s based on this data, route incoming MIDI messages to the appropriate `Module`(s), mix the audio output from all active `Module`s, and apply global effects (Reverb).
-   **Module**: A `Module` represents a single instrument or sound layer within the `Rack`, corresponding to one part in a `Performance`. Each `Module` is configured using part-specific data from a `Performance` object. It contains one or more `Dexed` instances (up to 4 for unison effects). The `Module` processes MIDI messages assigned to it, manages its `Dexed` instance(s), and applies module-level volume, panning, detune, and reverb send.
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
9. The final processed stereo audio is returned to the host.

## Class Diagram (Textual Description)

This section describes the main classes and their relationships.

### Class: `Rack`

*   **Responsibilities**:
    *   Manages a collection of `Module` instances.
    *   Loads multi-part performances from configuration files.
    *   Routes incoming MIDI messages to appropriate `Module`(s).
    *   Owns and manages and `Reverb` effect.
    *   Processes reverb sends from `Module`s and applies effects to the final mix.
    *   Mixes stereo audio output from all `Module`(s) and applies effects processing.
*   **Key Relationships**:
    *   **Aggregates**: `Module` (typically `std::vector<std::unique_ptr<Module>>`). A `Rack` *has* multiple `Module`s.
    *   **Aggregates**: `Reverb` effect.
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
    *   Stores all settings for a multi-part sound configuration, defining up to 16 parts. This includes, for each part, its complete DX7 voice data (as 156 hex bytes), MIDI channel, volume, panning, detune, transpose, note ranges, controller assignments, reverb send, etc.
    *   Is populated by parsing an INI file with numbered parameters (e.g., `Volume1`, `Pan2`, etc.).
    *   Provides the configuration data for each part to be used by the `Rack` during `Module` instantiation.
    *   Stores global effects settings (compressor and reverb parameters).
*   **Key Relationships**:
    *   **Contains**: Configuration data for multiple parts, including complete DX7 voice data (156 hex bytes per part), and various scalar properties for each part's configuration.
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
| - reverb        |             | - loadFromFile()  |
| - loadPerf()    |             +-------------------+
| - routeMidi()   |
| - mixAudio()    |
+-----------------+
       | 1                |1
       |                  |
       | aggregates       |owns
       | * (0..8)         |
+-----------------+      +----------+
|     Module      |      |  Reverb  |
|-----------------|      |----------|
| - dexed_engines |      | - size   |
| - volume        |      | - decay  |
| - pan           |      | - damping|
| - detune        |      | - wetdry |
| - reverb_send   |      +----------+
| - processMidi() |             ^
| - getAudio()    |             |
+-----------------+   receives reverb sends
       | 1
       |
       | aggregates
       | * (1..4)
+-----------------+
|    Dexed     |
|-----------------|
| - voice_params  |
| - note_range    |
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

The `Performance` configuration is stored in an INI file format with numbered parameters for each part. Based on the actual format, the structure is:

**Per-Part Parameters (1-8):**
Each part is defined by numbered parameters (e.g., `BankNumber1`, `Volume1`, etc.):
- `BankNumber[N]` - Bank identifier for voice selection (0-127)
- `VoiceNumber[N]` - Voice/voice number within the bank (0-127)
- `MIDIChannel[N]` - MIDI channel assignment (0=disabled, 1-16)
- `Volume[N]` - Part volume level (0-127)
- `Pan[N]` - Stereo panning (0-127, where 64=center)
- `Detune[N]` - Fine tuning in cents (-64 to +63)
- `Cutoff[N]` - Filter cutoff frequency (0-127)
- `Resonance[N]` - Filter resonance amount (0-127)
- `NoteLimitLow[N]`, `NoteLimitHigh[N]` - Note range limits (0-127)
- `NoteShift[N]` - Transpose in semitones (-24 to +24)
- `ReverbSend[N]` - Amount sent to reverb effect (0-127)
- `PitchBendRange[N]` - Pitch bend sensitivity in semitones (0-12)
- `PitchBendStep[N]` - Pitch bend step mode (0=smooth, 1=stepped)
- `PortamentoMode[N]` - Portamento on/off (0/1)
- `PortamentoGlissando[N]` - Glissando mode (0/1)
- `PortamentoTime[N]` - Portamento time (0-127)
- `VoiceData[N]` - Complete DX7 voice data as space-separated hex bytes (156 bytes)
- `MonoMode[N]` - Monophonic mode (0=poly, 1=mono)

**Controller Assignments per Part:**
- `ModulationWheelRange[N]`, `ModulationWheelTarget[N]` - Mod wheel assignment
- `FootControlRange[N]`, `FootControlTarget[N]` - Foot controller assignment
- `BreathControlRange[N]`, `BreathControlTarget[N]` - Breath controller assignment
- `AftertouchRange[N]`, `AftertouchTarget[N]` - Aftertouch assignment

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
- Each `Module` processes audio independently: Multiple Dexeds → Sum → Volume → Pan → Reverb Send split
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
- `*Range[N]` parameters define the amount of modulation (0-127)
- `*Target[N]` parameters define which synthesis parameter is modulated (implementation-specific mapping)

### Effects Parameters
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

## Multiprocessing and Thread Safety

FMRack is designed to be thread-safe, allowing for concurrent audio processing and MIDI handling. The architecture ensures that:
- Audio processing and MIDI message handling can occur in parallel
- The `Rack` and `Module` classes use mutexes to protect shared state during audio processing and MIDI routing
- Each `Dexed` instance operates independently, allowing for multiple voices to be processed simultaneously without contention

The command-line switch `--multiprocessing 0` can be used to disable multiprocessing for debugging or performance testing.

## User Interface (UI)

The FMRack architecture is primarily focused on audio processing and MIDI handling. The UI is supposed to communicate with FMRack via MIDI (e.g., over a virtual MIDI port or raw UDP port). This separation allows for flexibility in UI design and implementation, enabling various front-end solutions to interact with the core audio engine without direct dependencies.

## Testing

### Windows 11

FMRack has been tested on Windows 11.

### Raspbery Pi

FMRack has been tested on Raspberry Pi 5 with Raspberry Pi OS 64-bit.

* Start Raspberry Pi OS 64-bit
* Download the latest FMRack release from GitHub and Soundplantage performance files
* Extract the FMRack release to a suitable directory
* Connect a MIDI controller to the Raspberry Pi (USB or Bluetooth)
* Ensure the MIDI controller is recognized by the system (check with `./FMRack -h` to see available MIDI ports)
* Launch FMRack with the corresponding performance file and `-m` option to specify the MIDI device number:
  ```bash
  ./FMRack --performance /path/to/performance.ini -m <MIDI_Device_Number>
  ```
* To stop FMRack, press `Ctrl+C` in the terminal
* Test the MIDI controller by playing notes, adjusting parameters, and verifying audio output
* To switch to the command line, enter `sudo init 2`
* Run the same command as above to start FMRack in command-line mode
* To stop FMRack, press `Ctrl+C` in the terminal
* To return to the graphical interface, enter `sudo init 5`

## macOS

FMRack has not been tested on macOS yet. Contributions to test and ensure compatibility are welcome.
