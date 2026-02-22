# FMRack ESP32-S3 Port

Complete port of FMRack (the non-JUCE multi-timbral DX7 FM synthesizer rack) to the **ESP32-S3-DevKitC-1-N8R8** using ESP-IDF v5.4.

## Hardware

- **Board**: ESP32-S3-DevKitC-1-N8R8 (Espressif) (left port: USB Host, right port: USB CH340 Serial for flashing and logging)
- **CPU**: Dual-core Xtensa LX7, 240 MHz
- **Flash**: 8 MB (QIO 80 MHz)
- **PSRAM**: 8 MB (Octal SPI 80 MHz)
- **USB**: USB OTG peripheral — **USB Host** for class-compliant USB-MIDI keyboards
- **Audio output**: I2S stereo DAC with MCLK (e.g., PCM5102A, UDA1334A, MAX98357A, ES9018K2M)
- **MIDI input**: USB-MIDI keyboard (USB Host) + Hardware UART (31250 baud DIN-5)

## Features

- Up to 8 simultaneous DX7-compatible FM synth modules
- **Dual-core architecture**: Core 1 for real-time audio, core 0 for protocols
- **USB Host MIDI** — plug a USB-MIDI keyboard directly into the ESP32's USB port
- Hardware MIDI DIN input (UART at 31250 baud)
- UDP MIDI over WLAN
- SysEx support (4 KB buffer — handles DX7 32-voice bulk dumps)
- Performance file loading from SPIFFS
- I2S stereo audio output (16-bit, 48 kHz) with MCLK
- LED status indicator (addressable RGB on GPIO48)

## Quick Start — How to Use

### What You Need

1. **ESP32-S3-DevKitC-1-N8R8** (or compatible S3 board with 8 MB PSRAM)
2. **I2S DAC board** (e.g., PCM5102A breakout) — for audio output
3. **USB-C data cable** (not charge-only)
4. A MIDI source: USB-MIDI keyboard/controller, or classic DIN-5 MIDI keyboard

### USB Ports on the DevKit

The ESP32-S3-DevKitC-1 has **two USB-C connectors**:

```
  ┌──────────────────────────────────────┐
  │        ESP32-S3-DevKitC-1            │
  │                                      │
  │  [UART]  ← Serial console / flash   │
  │  [USB]   ← USB Host (MIDI keyboard) │
  │                                      │
  └──────────────────────────────────────┘
```

| Port   | Purpose                       | What to connect              |
|--------|-------------------------------|------------------------------|
| **UART** | Serial console + flashing   | USB cable to your PC         |
| **USB**  | USB Host for MIDI keyboard  | USB-MIDI keyboard/controller |

> **Important**: The console/log output goes to the **UART** port over UART0.
> The **USB** port acts as a **USB Host** — plug your USB-MIDI keyboard here.
> The ESP32 supplies 5V power and reads MIDI from the attached keyboard.

### Wiring the I2S DAC

Connect an I2S DAC module to the ESP32-S3:

| ESP32-S3 GPIO | DAC Pin  | Signal     |
|---------------|----------|------------|
| GPIO 0        | MCLK     | Master Clock (256×fs = 12.288 MHz) |
| GPIO 5        | BCK      | Bit Clock  |
| GPIO 6        | WS/LRCK  | Word Select / Left-Right Clock |
| GPIO 7        | DIN/SDIN | Serial Data |
| 3.3V          | VCC      | Power      |
| GND           | GND      | Ground     |

> **Tip**: If your DAC doesn't need MCLK (like some PCM5102A boards with an on-board oscillator), leave GPIO 0 unconnected. The firmware still outputs MCLK; it's harmless.

### Wiring Hardware MIDI DIN (Optional)

For a classic 5-pin DIN MIDI input, connect through an optocoupler (6N138 or H11L1):

| ESP32-S3 GPIO | Function  |
|---------------|-----------|
| GPIO 18       | MIDI RX (UART1, 31250 baud) |
| GPIO 17       | MIDI TX (optional, for MIDI thru) |

Standard MIDI-IN optocoupler circuit required (see any MIDI hardware tutorial).

### Flashing the Firmware

```bash
# First time: put the board in download mode
# Hold BOOT → Press RST → Release BOOT
# Then:
cd esp32
./build.sh flash        # or: idf.py -p /dev/ttyACM0 flash

# After flashing, press RST to boot normally
```

### Sending MIDI to FMRack

#### Option 1: USB-MIDI Keyboard (USB Host)

1. Plug a class-compliant USB-MIDI keyboard into the **USB** port on the DevKit
2. The ESP32 detects the keyboard automatically (hot-plug supported)
3. Play notes — audio comes out of the I2S DAC

The serial console (UART port) shows:
```
I (3200) fmrack_midi: New USB device at address 1
I (3210) fmrack_midi: USB device: VID=0x1234 PID=0x5678 class=0
I (3220) fmrack_midi: Found MIDI Streaming interface 1 (2 endpoints)
I (3230) fmrack_midi: USB MIDI keyboard connected -- streaming from EP 0x81
```

> **Note**: The USB port provides 5V to the keyboard. Most bus-powered
> USB-MIDI controllers work fine. If you need more power (e.g., for
> a keyboard with lights), use a powered USB hub between the ESP32 and keyboard.

#### Option 2: Hardware MIDI DIN

1. Wire the MIDI optocoupler circuit to GPIO 18 (RX)
2. Connect your MIDI keyboard's MIDI OUT to the optocoupler's MIDI IN
3. Play — notes are processed immediately

#### Option 3: UDP MIDI over WLAN

1. Configure WLAN credentials via `idf.py menuconfig` → FMRack Configuration
2. After boot, the ESP32 connects to WLAN and listens for UDP MIDI on port 21928
3. Send raw MIDI bytes via UDP from any software (e.g., `sendmidi`, custom scripts)

### Sending SysEx Voice Data

FMRack accepts DX7-compatible SysEx voice data:

- **Single voice** (VCED, 163 bytes): Changes the voice on one module
- **32-voice bulk dump** (4104 bytes): Loads a bank of 32 voices

Send SysEx via USB-MIDI keyboard, DIN MIDI, or UDP.

### Monitoring Serial Output

Connect the **UART** port to your PC and run:
```bash
idf.py -p /dev/ttyUSB0 monitor    # if board has CP2102N on UART port
# or
idf.py -p /dev/ttyACM0 monitor    # if using USB Serial/JTAG (during debug)
```

> **Note**: The **USB** port is a USB Host for keyboards and cannot be used as
> a serial console. Connect the **UART** port for serial logs.
> For development, connect **both** USB ports — UART for logs, USB for keyboard.

## Pin Assignments

| Function       | GPIO Pin | Notes                              |
|----------------|----------|------------------------------------|
| I2S_MCLK       | GPIO 0   | Master Clock output (256×fs)       |
| I2S_BCK        | GPIO 5   | I2S Bit Clock                      |
| I2S_WS         | GPIO 6   | I2S Word Select (LRCLK)           |
| I2S_DOUT       | GPIO 7   | I2S Data Out                       |
| MIDI_RX        | GPIO 18  | UART1 RX (31250 baud)             |
| MIDI_TX        | GPIO 17  | UART1 TX (optional MIDI thru)      |
| USB_D-         | GPIO 19  | USB OTG Data- (USB Host MIDI)      |
| USB_D+         | GPIO 20  | USB OTG Data+ (USB Host MIDI)      |
| STATUS_LED     | GPIO 48  | Addressable RGB LED (DevKitC-1)    |

All pins are configurable at compile time in `main/esp32_config.h`.

## Build Instructions

### Prerequisites

1. **ESP-IDF v5.4** or later: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/
2. Set up the environment:
   ```bash
   . ~/esp/esp-idf/export.sh
   ```

### Full Build and Flash

```bash
cd esp32

# First time (or after changing sdkconfig.defaults):
idf.py set-target esp32s3
idf.py build

# Flash (put board in BOOT mode if needed):
idf.py -p /dev/ttyACM0 flash

# Monitor console output (connect UART port):
idf.py -p /dev/ttyUSB0 monitor
```

Or use the build script:
```bash
./build.sh              # build only
./build.sh flash        # build + flash
./build.sh monitor      # build + flash + monitor
```

### Clean Rebuild

If you change `sdkconfig.defaults`, do a full clean rebuild:
```bash
rm -rf build sdkconfig
idf.py set-target esp32s3
idf.py build
```

### Upload Performance Files to SPIFFS

Performance `.ini` files and `.syx` voice banks go into the SPIFFS partition:

```bash
# The build script generates spiffs.bin from the spiffs_data/ directory
# Flash it to the storage partition:
python -m esptool --chip esp32s3 write_flash 0x310000 build/spiffs.bin
```

Place your files in `spiffs_data/` before building:
```
spiffs_data/
├── default.ini          # Default performance configuration
├── DX7_ROM1A.syx        # Voice bank (32 voices)
└── ...
```

## Architecture

```
              ESP32-S3 Dual-Core (240 MHz)
  ┌────────────────────┬─────────────────────────┐
  │      Core 0        │        Core 1            │
  │    (protocol)      │    (real-time audio)      │
  │                    │                           │
  │  ┌──────────────┐  │  ┌──────────────────────┐ │
  │  │ MIDI UART    │  │  │   FMRack Engine      │ │
  │  │ (DIN-5)      │──┤  │  ┌────────────────┐  │ │
  │  └──────────────┘  │  │  │ Module 1..8    │  │ │    ┌──────────┐
  │  ┌──────────────┐  │  │  │ (DX7 voices)   │  │ │───→│ I2S DAC  │──→ Audio
  │  │ USB Host     │──┤  │  └────────────────┘  │ │    │ + MCLK   │
  │  │ MIDI (OTG)   │  │  │  ┌────────────────┐  │ │    └──────────┘
  │  └──────────────┘  │  │  │ Plate Reverb   │  │ │
  │  ┌──────────────┐  │  │  └────────────────┘  │ │
  │  │ UDP MIDI     │──┤  │                      │ │
  │  │ (WLAN)      │  │  │  processAudio()      │ │
  │  └──────────────┘  │  │  → I2S DMA write     │ │
  │  ┌──────────────┐  │  │                      │ │
  │  │ Status LED   │  │  └──────────────────────┘ │
  │  └──────────────┘  │                           │
  └────────────────────┴─────────────────────────┘
```

### Boot Sequence

1. **Phase 1**: NVS flash init
2. **Phase 2**: SPIFFS mount + load performance files
3. **Phase 3**: FMRack engine init (modules allocated in PSRAM)
4. **Phase 4**: I2S audio output start (48 kHz, audio task on core 1)
5. **Phase 5**: MIDI init (UART + USB Host MIDI)
6. **Phase 6**: WLAN + UDP MIDI (if configured)

After phase 5, the USB port enters Host mode — plug in a USB-MIDI keyboard.

## Memory Layout

- **Rack + modules**: Allocated in PSRAM (~50 KB per module)
- **Audio DMA buffers**: Internal SRAM (DMA-capable)
- **Plate reverb delay lines**: PSRAM
- **SysEx buffers**: 4 KB each for UART and USB Host MIDI (internal SRAM)
- **USB Host transfers**: Internal SRAM (DMA-capable, for USB bulk reads)

Firmware binary: ~1.2 MB (61% free in the 3 MB app partition).
With 8 MB PSRAM, 8 modules fit comfortably with room to spare.

## Configuration

Use `idf.py menuconfig` to adjust settings. Key options:

| Category       | Option                  | Default    |
|----------------|-------------------------|------------|
| Audio           | Sample rate            | 48000 Hz   |
| Audio           | Buffer size            | 256 samples|
| Audio           | Number of modules      | 8          |
| FMRack          | Engine type            | MSFA       |
| I2S Pins        | BCK / WS / DOUT / MCLK| 5 / 6 / 7 / 0 |
| MIDI            | UART RX / TX pins      | 18 / 17    |
| MIDI            | USB-MIDI enable        | Yes        |
| WLAN           | SSID / Password        | (empty)    |
| Performance     | Default file path      | /spiffs/default.ini |

## Troubleshooting

### No serial output after boot
The console goes to the **UART** port (UART0, GPIO43/44), not the USB port.
Connect a cable to the UART connector. The USB port is a USB Host for keyboards.

### USB keyboard not detected
- Check that the keyboard is class-compliant USB-MIDI (no special drivers needed)
- Monitor serial output for `New USB device at address` and `MIDI Streaming interface` messages
- Try a powered USB hub if the keyboard draws too much current
- Non-MIDI USB devices are gracefully ignored (logged as "not a MIDI device")

### Cannot flash — device not in download mode
Hold BOOT → Press RST → Release BOOT. The device enters download mode and
shows as `/dev/ttyACM0` (Espressif USB JTAG/serial debug unit).

### No audio output
- Verify I2S DAC wiring (BCK, WS, DOUT, optionally MCLK)
- Ensure the DAC is powered (3.3V or 5V depending on module)
- Send MIDI note-on messages to verify the engine is responding
- Check that at least one module has a voice loaded (default performance should load voices from SPIFFS)
