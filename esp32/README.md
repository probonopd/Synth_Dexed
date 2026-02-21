# FMRack ESP32-C5 Port

Complete port of FMRack (the non-JUCE multi-timbral DX7 FM synthesizer rack) to the **ESP32-C5-DevKitC-1-N8R8** using ESP-IDF.

## Hardware

- **Board**: ESP32-C5-DevKitC-1-N8R8
- **CPU**: RISC-V single-core, up to 240 MHz
- **Flash**: 8 MB
- **PSRAM**: 8 MB
- **Audio output**: I2S stereo DAC (e.g., PCM5102A, MAX98357A, or UDA1334A)
- **MIDI input**: UART (31250 baud) via optocoupler circuit

## Features

- Up to 8 simultaneous FM synth modules (DX7-compatible)
- Stereo plate reverb effect
- Hardware MIDI input (UART at 31250 baud, standard DIN-5)
- USB MIDI input (via USB-CDC)
- UDP MIDI over Wi-Fi
- Performance file loading from SPIFFS
- I2S stereo audio output (16-bit, 48 kHz)
- LED status indicator

## Pin Assignments

| Function       | GPIO Pin | Notes                        |
|----------------|----------|------------------------------|
| I2S_BCK        | GPIO 6   | I2S Bit Clock                |
| I2S_WS         | GPIO 7   | I2S Word Select (LRCLK)     |
| I2S_DOUT       | GPIO 15  | I2S Data Out                 |
| MIDI_RX        | GPIO 4   | UART1 RX (31250 baud)       |
| MIDI_TX        | GPIO 5   | UART1 TX (optional)          |
| STATUS_LED     | GPIO 8   | Onboard LED                  |

## Build Instructions

### Prerequisites

1. Install ESP-IDF v5.3 or later (with ESP32-C5 support)
2. Set up the environment: `. $IDF_PATH/export.sh`

### Build and Flash

```bash
cd esp32
idf.py set-target esp32c5
idf.py build
idf.py -p /dev/ttyUSBx flash monitor
```

### Upload Performance Files

Performance `.ini` files and `.syx` voice banks can be uploaded to the SPIFFS partition:

```bash
# Create the spiffs image
python -m esptool --chip esp32c5 write_flash 0x310000 spiffs.bin
```

Or use the menuconfig to set up SPIFFS:
```bash
idf.py menuconfig
```

## Architecture

```
┌─────────────────────────────────────────┐
│              ESP32-C5                    │
│                                         │
│  ┌──────────┐  ┌──────────────────────┐ │
│  │ MIDI In  │  │     FMRack Engine    │ │
│  │ (UART)   │──│  ┌────────────────┐  │ │
│  └──────────┘  │  │  Module 1..8   │  │ │    ┌──────────┐
│  ┌──────────┐  │  │  (Dexed x1-4)  │  │ │───→│ I2S DAC  │──→ Audio Out
│  │ UDP MIDI │  │  └────────────────┘  │ │    └──────────┘
│  │ (Wi-Fi)  │──│  ┌────────────────┐  │ │
│  └──────────┘  │  │  Plate Reverb  │  │ │
│  ┌──────────┐  │  └────────────────┘  │ │
│  │ USB MIDI │──│                      │ │
│  │ (CDC)    │  │  Performance/Config  │ │
│  └──────────┘  └──────────────────────┘ │
└─────────────────────────────────────────┘
```

## Memory Considerations

- The Dexed engine is allocated in PSRAM to conserve internal SRAM
- Audio buffers use DMA-capable internal memory
- The plate reverb delay lines are placed in PSRAM
- With 8 MB PSRAM, up to 8 modules (each ~50 KB) fit comfortably
- Sample rate: 48 kHz (configurable down to 22050 Hz for lower CPU usage)
- Buffer size: 256 samples (configurable)

## Configuration

Use `idf.py menuconfig` → "FMRack Configuration" to adjust:

- Sample rate (22050 / 44100 / 48000)
- Buffer size (64 / 128 / 256 / 512)
- Number of modules (1-8)
- I2S pin assignments
- MIDI UART pin assignments  
- Wi-Fi SSID/password for UDP MIDI
- Default performance file path
