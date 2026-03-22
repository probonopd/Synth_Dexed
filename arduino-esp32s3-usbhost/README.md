# ESP32-S3 Arduino USB-Host Dexed (from scratch)

This folder is a **clean Arduino port** for ESP32-S3 that:

- runs the Dexed engine from this repo’s Arduino library sources (root [src/](../src/))
- outputs audio via **I2S** to an external DAC
- reads a **class-compliant USB-MIDI keyboard via USB Host**, including **hub support**
- boots with the patch **E.PIANO 1** loaded

It does **not** use any code from the existing ESP-IDF port in [esp32/](../esp32/).

## Hardware

- ESP32-S3 board with native USB-OTG wired to GPIO19/20 (e.g. **ESP32-S3-DevKitC-1**)
- USB-A host connector (or OTG adapter) wired:
  - GPIO19 → D-
  - GPIO20 → D+
  - 5V + GND to the USB port
- USB hub (powered recommended) + USB-MIDI keyboard
- I2S DAC (PCM5102A / MAX98357A / etc.)

Default I2S pins in the sketch:

- MCLK: GPIO0 (optional)
- BCK: GPIO5
- WS/LRCK: GPIO6
- DOUT: GPIO7

## Build (PlatformIO)

This project uses **Arduino as a component on top of ESP-IDF**, because Arduino-ESP32 alone does not (currently) provide a stable high-level USB-host API.

1) Install PlatformIO (VS Code extension or CLI)

2) Build & upload:

```bash
cd arduino-esp32s3-usbhost
pio run -t upload
pio device monitor
```

## “NodeMCU” note

Many boards sold as “NodeMCU” are **ESP32 (not S3)** and do **not** have the ESP32-S3 USB-OTG host peripheral on GPIO19/20.

- If you really have an **ESP32-S3** board, keep `board = esp32-s3-devkitc-1` in [platformio.ini](platformio.ini).
- If you have a different ESP32-S3 variant, change the `board = ...` line accordingly (and keep USB D-/D+ on GPIO19/20).

## Notes

- If your USB keyboard draws significant current (LEDs, etc.), use a **powered hub**.
- If you change pins/sample-rate/buffer sizes, adjust them in [src/main.cpp](src/main.cpp).
