# ESP32-S3 Dev Module + Estardyn 1.3 SH1106 OLED + EC11 Rotary Encoder pin map

## ESP32-S3 DevKitC-1 / Dev module 2x19 header

## Estardyn 1.3 OLED + rotary encoder module (confirmed pin order)

| pin pos | signal | notes | ESP32S3 pin |
|---------|--------|-------|-----|
| 1 | CONFIRM_BTN | Confirm button | GPIO46 |
| 2 | OLED_SDA | I2C data | GPIO 9 |
| 3 | OLED_SCL | I2C clock | GPIO 10 |
| 4 | ENCODER_PUSH | encoder button | GPIO 11 |
| 5 | ENCODER_TRA | encoder A | GPIO 12 |
| 6 | ENCODER_TRB | encoder B | GPIO 13 |
| 7 | BAK_BTN | Back button | GPIO 14 |
| 8 | GND | ground | GND |
| 9 | 3V3-5V | power input | 3V3 |

## notes

- Use 3.3V supply. Avoid 5V to the ESP32 GPIO rail.
- Confirm the module’s silkscreen for that particular PCB revision.
- Avoid using flash/PSRAM pins IO6–IO11 for extra peripherals unless absolutely required.
