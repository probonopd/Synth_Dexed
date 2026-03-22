/*
 * FMRack ESP32-S3 Port - Configuration Header
 *
 * Centralizes all compile-time configuration for the ESP32-S3 port.
 * Leverages dual-core Xtensa LX7, 8 MB PSRAM, and USB OTG.
 * Values come from Kconfig (menuconfig) with sensible defaults.
 */

#pragma once

#include "sdkconfig.h"

// =====================
// Audio Configuration
// =====================
// Sample rate.  With the LUT and audio-path optimizations (reduced sine/exp2
// tables, int16-only rendering) the engine comfortably handles 48 kHz (and
// 96 kHz on lightly-loaded systems).  Higher rates improve audio fidelity but
// increase CPU cost.  This value is now configurable via menuconfig (see
// FMRACK_SAMPLE_RATE) so you can tweak it for "hifi" builds.
#ifdef CONFIG_FMRACK_SAMPLE_RATE
#define FMRACK_SAMPLE_RATE      CONFIG_FMRACK_SAMPLE_RATE
#else
#define FMRACK_SAMPLE_RATE      48000
#endif
// Polyphony (number of simultaneous notes)
#ifdef CONFIG_FMRACK_POLYPHONY
#define FMRACK_POLYPHONY        CONFIG_FMRACK_POLYPHONY
#else
#define FMRACK_POLYPHONY        12
#endif
#ifdef CONFIG_FMRACK_BUFFER_SIZE
#define FMRACK_BUFFER_SIZE      CONFIG_FMRACK_BUFFER_SIZE
#else
#define FMRACK_BUFFER_SIZE      256
#endif

// Select which Dexed engine to use. 0 = MSFA (modern), 1 = MKI (legacy),
// 2 = OPL.  MSFA has the best performance and is now the default.  You can
// override this from menuconfig by setting CONFIG_FMRACK_ENGINE or by editing
// sdkconfig directly.
#ifdef CONFIG_FMRACK_ENGINE
#define FMRACK_ENGINE           CONFIG_FMRACK_ENGINE
#else
#define FMRACK_ENGINE           0
#endif


#ifdef CONFIG_FMRACK_NUM_MODULES
#define FMRACK_NUM_MODULES      CONFIG_FMRACK_NUM_MODULES
#else
#define FMRACK_NUM_MODULES      8
#endif

// =====================
// I2S Pin Configuration (ESP32-S3 DevKitC defaults)
// =====================
#ifdef CONFIG_FMRACK_I2S_BCK_PIN
#define FMRACK_I2S_BCK_PIN     CONFIG_FMRACK_I2S_BCK_PIN
#else
#define FMRACK_I2S_BCK_PIN     5
#endif

#ifdef CONFIG_FMRACK_I2S_WS_PIN
#define FMRACK_I2S_WS_PIN      CONFIG_FMRACK_I2S_WS_PIN
#else
#define FMRACK_I2S_WS_PIN      6
#endif

#ifdef CONFIG_FMRACK_I2S_DOUT_PIN
#define FMRACK_I2S_DOUT_PIN    CONFIG_FMRACK_I2S_DOUT_PIN
#else
#define FMRACK_I2S_DOUT_PIN    7
#endif

#ifdef CONFIG_FMRACK_I2S_MCLK_PIN
#define FMRACK_I2S_MCLK_PIN    CONFIG_FMRACK_I2S_MCLK_PIN
#else
#define FMRACK_I2S_MCLK_PIN    1
#endif

#ifdef CONFIG_FMRACK_I2S_NUM
#define FMRACK_I2S_NUM         CONFIG_FMRACK_I2S_NUM
#else
#define FMRACK_I2S_NUM         0
#endif

// =====================
// MIDI Configuration
// =====================
#ifdef CONFIG_FMRACK_MIDI_UART_NUM
#define FMRACK_MIDI_UART_NUM   CONFIG_FMRACK_MIDI_UART_NUM
#else
#define FMRACK_MIDI_UART_NUM   1
#endif

#ifdef CONFIG_FMRACK_MIDI_RX_PIN
#define FMRACK_MIDI_RX_PIN     CONFIG_FMRACK_MIDI_RX_PIN
#else
#define FMRACK_MIDI_RX_PIN     18
#endif

#ifdef CONFIG_FMRACK_MIDI_TX_PIN
#define FMRACK_MIDI_TX_PIN     CONFIG_FMRACK_MIDI_TX_PIN
#else
#define FMRACK_MIDI_TX_PIN     17
#endif

// USB-MIDI via USB OTG (ESP32-S3 native USB, GPIO19=D-, GPIO20=D+)
#ifdef CONFIG_FMRACK_MIDI_USB_ENABLE
#define FMRACK_MIDI_USB_ENABLE CONFIG_FMRACK_MIDI_USB_ENABLE
#else
#define FMRACK_MIDI_USB_ENABLE 1
#endif

#ifdef CONFIG_FMRACK_MIDI_UDP_ENABLE
#define FMRACK_MIDI_UDP_ENABLE 1
#else
#define FMRACK_MIDI_UDP_ENABLE 0
#endif

#ifdef CONFIG_FMRACK_MIDI_UDP_PORT
#define FMRACK_MIDI_UDP_PORT   CONFIG_FMRACK_MIDI_UDP_PORT
#else
#define FMRACK_MIDI_UDP_PORT   50007
#endif

// =====================
// Wi-Fi Configuration
// =====================
#ifdef CONFIG_FMRACK_WIFI_SSID
#define FMRACK_WIFI_SSID       CONFIG_FMRACK_WIFI_SSID
#else
#define FMRACK_WIFI_SSID       "FMRack"
#endif

#ifdef CONFIG_FMRACK_WIFI_PASSWORD
#define FMRACK_WIFI_PASSWORD   CONFIG_FMRACK_WIFI_PASSWORD
#else
#define FMRACK_WIFI_PASSWORD   ""
#endif

#ifdef CONFIG_FMRACK_WIFI_AP_MODE
#define FMRACK_WIFI_AP_MODE    1
#else
#define FMRACK_WIFI_AP_MODE    0
#endif

// =====================
// Storage Configuration
// =====================
#ifdef CONFIG_FMRACK_SPIFFS_MOUNT_POINT
#define FMRACK_SPIFFS_MOUNT    CONFIG_FMRACK_SPIFFS_MOUNT_POINT
#else
#define FMRACK_SPIFFS_MOUNT    "/spiffs"
#endif

#ifdef CONFIG_FMRACK_DEFAULT_PERFORMANCE
#define FMRACK_DEFAULT_PERF    CONFIG_FMRACK_DEFAULT_PERFORMANCE
#else
#define FMRACK_DEFAULT_PERF    "/spiffs/performance.ini"
#endif

// =====================
// Hardware
// =====================
// ESP32-S3-DevKitC-1 has an addressable RGB LED on GPIO48
#ifdef CONFIG_FMRACK_STATUS_LED_PIN
#define FMRACK_STATUS_LED_PIN  CONFIG_FMRACK_STATUS_LED_PIN
#else
#define FMRACK_STATUS_LED_PIN  48
#endif

// BOOT button on GPIO 0 (pulled high internally, active-low)
#define FMRACK_BOOT_BUTTON_PIN  0

// =====================
// Audio processing constants
// =====================
// I2S DMA ring: 8 descriptors x 256 frames = ~46 ms DMA buffer headroom.
// The Dexed object is in internal SRAM so USB crackles are already gone.
// 8 descriptors gives ample margin against WiFi-induced stalls (DTIM beacon
// wakeup, mDNS responses, Apple MIDI session CK packets) without adding
// perceptible latency.
#define FMRACK_I2S_DMA_BUF_COUNT        8
#define FMRACK_I2S_DMA_BUF_LEN         FMRACK_BUFFER_SIZE

// Pre-render ring between render task and write task.
// 8 blocks = ~46 ms headroom.  When WLAN is active together with a USB
// keyboard, WiFi DMA (beacon wakeup bursts) and USB enumeration DMA both
// hammer the AHB bus simultaneously, causing 10–30 ms stalls.  4 blocks
// (~23 ms) wasn't enough to bridge the combined stall; 8 blocks absorb it
// completely.  Cost is still modest: 8 × 256 × 4 bytes = 8 KB in DRAM.
#define FMRACK_AUDIO_PRERENDER_BLOCKS   8

// MIDI baud rate (standard)
#define MIDI_BAUD_RATE              31250
#define MIDI_UART_BUF_SIZE          512

// Task priorities (higher = more important)
#define AUDIO_TASK_PRIORITY         (configMAX_PRIORITIES - 1)  // render task
#define AUDIO_WRITE_TASK_PRIORITY   (configMAX_PRIORITIES - 2)  // write task (same core, lower)
#define MIDI_TASK_PRIORITY          (configMAX_PRIORITIES - 2)
#define USB_MIDI_TASK_PRIORITY      (configMAX_PRIORITIES - 2)
#define UDP_TASK_PRIORITY           (configMAX_PRIORITIES - 3)
#define WIFI_TASK_PRIORITY          5
#define STATUS_TASK_PRIORITY        2

// Task stack sizes
#define AUDIO_TASK_STACK_SIZE       (8 * 1024)
#define AUDIO_WRITE_TASK_STACK_SIZE (4 * 1024)
#define MIDI_TASK_STACK_SIZE        (4 * 1024)
#define USB_MIDI_TASK_STACK_SIZE    (4 * 1024)
#define UDP_TASK_STACK_SIZE         (4 * 1024)
#define STATUS_TASK_STACK_SIZE      (2 * 1024)

// =====================
// Apple MIDI (RTP-MIDI) configuration
// =====================

// Base UDP port.  The control port is FMRACK_APPLEMIDI_PORT, the data port
// is FMRACK_APPLEMIDI_PORT + 1 (default 5004 / 5005 — IANA registered).
#ifdef CONFIG_FMRACK_APPLEMIDI_PORT
#define FMRACK_APPLEMIDI_PORT  CONFIG_FMRACK_APPLEMIDI_PORT
#else
#define FMRACK_APPLEMIDI_PORT  5004
#endif

// Session / service name advertised via mDNS and shown in Audio MIDI Setup.
#ifdef CONFIG_FMRACK_APPLEMIDI_NAME
#define FMRACK_APPLEMIDI_NAME  CONFIG_FMRACK_APPLEMIDI_NAME
#else
#define FMRACK_APPLEMIDI_NAME  "FMSynthESP"
#endif

// =====================
// Captive portal AP SSID (open network, no password)
// =====================
#ifdef CONFIG_FMRACK_CAPTIVE_AP_SSID
#define FMRACK_CAPTIVE_AP_SSID CONFIG_FMRACK_CAPTIVE_AP_SSID
#else
#define FMRACK_CAPTIVE_AP_SSID "Synth-Dexed-Setup"
#endif

// =====================
// Apple MIDI task parameters
// =====================
#define APPLEMIDI_TASK_STACK_SIZE   (6 * 1024)
#define APPLEMIDI_TASK_PRIORITY     (configMAX_PRIORITIES - 3)

// =====================
// Dual-core task affinity (ESP32-S3 has 2 cores)
// Core 0: Protocol tasks (MIDI, Wi-Fi, USB)
// Core 1: Real-time audio rendering (dedicated)
// =====================
#define AUDIO_TASK_CORE             1
#define MIDI_TASK_CORE              0
#define USB_MIDI_TASK_CORE          0
#define WIFI_TASK_CORE              0
